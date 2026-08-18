#include "processes.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <engine/array.h>
#include <engine/log.h>
#include <engine/macros.h>
#include <engine/vertex.h>

#include "renderer/descriptor_set.h"
#include "renderer/draw.h"
#include "renderer/pipeline.h"
#include "renderer/uniform_buffer.h"
#include "renderer/vk_buffer.h"
#include "renderer/vk_images.h"

#include "city.h"
#include "swordfish.h"

Processes processes;

//the ring starts outside the cpu die and grows outwards, same shape the
//directory city uses
#define PROCESS_INNER_RADIUS 30.0f
#define PROCESS_RING_GAP 1.0f
#define PROCESS_FOOTPRINT 3.0f
#define PROCESS_BLOCK_GAP 1.0f

//a live process is always visible even at zero cpu, so the ring reads as the
//process table. a dead slot collapses to nothing instead
#define PROCESS_MIN_HEIGHT 0.9f
#define PROCESS_MAX_HEIGHT 22.0f

//the cpu fraction that reaches full height. one saturated thread
#define PROCESS_CPU_FULL 1.0f

//resident set that reaches the hot end of the colour ramp, in megabytes. log
//scaled, because almost every process is tiny and a few are enormous
#define PROCESS_MEMORY_FULL_MB 4096.0f

#define PROCESS_SMOOTHING 0.10f

#define PROCESS_SAMPLE_INTERVAL_US 500000
#define PROCESS_SLEEP_CHUNKS 10

#define PROCESS_TAU 6.28318530f

static float process_clamp(float value, float low, float high) {
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

//own clock instead of delta_time, which never advances its counter
static float process_elapsed_seconds(void) {

  static struct timespec start;
  static bool started = false;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  if (!started) {
    start = now;
    started = true;
  }

  return (float)(now.tv_sec - start.tv_sec) +
         (float)(now.tv_nsec - start.tv_nsec) / 1000000000.0f;
}

//jiffies across every core, the denominator a process's own time is measured
//against
static u64 process_read_total_jiffies(void) {

  FILE *file = fopen("/proc/stat", "r");
  if (!file)
    return 0;

  char line[512];
  u64 total = 0;

  if (fgets(line, sizeof(line), file) != NULL) {
    u64 field[8];
    ZERO(field);

    if (sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu", &field[0],
               &field[1], &field[2], &field[3], &field[4], &field[5],
               &field[6], &field[7]) >= 4)
      for (int i = 0; i < 8; i++)
        total += field[i];
  }

  fclose(file);
  return total;
}

static u64 process_read_total_memory_kb(void) {

  FILE *file = fopen("/proc/meminfo", "r");
  if (!file)
    return 0;

  char line[256];
  u64 total = 0;

  while (fgets(line, sizeof(line), file) != NULL)
    if (sscanf(line, "MemTotal: %lu", &total) == 1)
      break;

  fclose(file);
  return total;
}

//same packing the city uses, 4 characters per uint
static void process_pack_name(u32 *words, float *length, const char *name) {

  for (int i = 0; i < PINSTANCE_NAME_WORDS; i++)
    words[i] = 0;

  int count = 0;
  while (name[count] != '\0' && count < PINSTANCE_NAME_MAX)
    count++;

  for (int i = 0; i < count; i++) {
    u32 character = (u32)(unsigned char)name[i];
    words[i >> 2] |= character << (8 * (i & 3));
  }

  *length = (float)count;
}

//comm can hold spaces and brackets of its own, so the name is taken between
//the first ( and the last ), and the numeric fields start after that.
//virtual_size comes back too: a kernel thread has no address space, so zero
//there is what separates them from real processes
static bool process_read_stat(int pid, char *name, int name_size, u64 *cpu,
                              long *resident_pages, u64 *virtual_size) {

  char path[64];
  snprintf(path, sizeof(path), "/proc/%i/stat", pid);

  FILE *file = fopen(path, "r");
  if (!file)
    return false;

  char line[1024];
  char *read = fgets(line, sizeof(line), file);
  fclose(file);

  if (!read)
    return false;

  char *open_bracket = strchr(line, '(');
  char *close_bracket = strrchr(line, ')');

  if (!open_bracket || !close_bracket || close_bracket < open_bracket)
    return false;

  int length = (int)(close_bracket - open_bracket - 1);
  if (length >= name_size)
    length = name_size - 1;

  memcpy(name, open_bracket + 1, length);
  name[length] = '\0';

  char state;
  int ppid, pgrp, session, tty, tpgid;
  unsigned flags;
  unsigned long minflt, cminflt, majflt, cmajflt, utime, stime;
  long cutime, cstime, priority, nice, threads, itreal;
  unsigned long long starttime;
  unsigned long vsize;
  long resident;

  int fields = sscanf(close_bracket + 1,
                      " %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld "
                      "%ld %ld %ld %ld %llu %lu %ld",
                      &state, &ppid, &pgrp, &session, &tty, &tpgid, &flags,
                      &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
                      &cutime, &cstime, &priority, &nice, &threads, &itreal,
                      &starttime, &vsize, &resident);

  if (fields < 22)
    return false;

  *cpu = (u64)utime + (u64)stime;
  *resident_pages = resident;
  *virtual_size = (u64)vsize;

  return true;
}

//find the slot this pid already owns, or take a free one. the slot keeps its
//ring position, so reusing one does not move anything on screen
static int process_slot_for(Processes *target, int pid) {

  int free_slot = -1;

  for (int i = 0; i < PROCESS_MAX; i++) {
    if (target->slots[i].pid == pid)
      return i;
    if (free_slot < 0 && target->slots[i].pid == 0)
      free_slot = i;
  }

  if (free_slot < 0)
    return -1;

  target->slots[free_slot].pid = pid;
  target->slots[free_slot].cpu_jiffies = 0;
  target->slots[free_slot].cpu = 0.0f;
  target->slots[free_slot].memory = 0.0f;

  return free_slot;
}

static void process_sample(Processes *target) {

  u64 total_jiffies = process_read_total_jiffies();
  u64 total_delta = total_jiffies - target->previous_total_jiffies;
  target->previous_total_jiffies = total_jiffies;

  for (int i = 0; i < PROCESS_MAX; i++)
    target->slots[i].seen = false;

  DIR *proc = opendir("/proc");
  if (!proc)
    return;

  long page_kb = sysconf(_SC_PAGESIZE) / 1024;

  //names only change when a slot is reused, but they are cheap to restate
  u32 name_words[PROCESS_MAX][PINSTANCE_NAME_WORDS];
  float name_length[PROCESS_MAX];
  ZERO(name_words);
  ZERO(name_length);

  struct dirent *entry;
  while ((entry = readdir(proc)) != NULL) {

    if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
      continue;

    int pid = atoi(entry->d_name);
    if (pid <= 0)
      continue;

    char name[PINSTANCE_NAME_MAX + 1];
    u64 cpu_jiffies;
    long resident_pages;
    u64 virtual_size;

    if (!process_read_stat(pid, name, sizeof(name), &cpu_jiffies,
                           &resident_pages, &virtual_size))
      continue;

    //kernel threads have no address space. on this machine they are about
    //nine tenths of /proc and none of them ever move, so they would bury the
    //processes worth looking at under a carpet
    if (virtual_size == 0)
      continue;

    int slot = process_slot_for(target, pid);
    if (slot < 0)
      continue;

    ProcessSlot *entry_slot = &target->slots[slot];
    entry_slot->seen = true;

    //a slot on its first sample has no previous reading to subtract from
    if (entry_slot->cpu_jiffies > 0 && total_delta > 0) {
      u64 delta = cpu_jiffies - entry_slot->cpu_jiffies;
      //total_delta counts every core, so scaling by the core count turns it
      //back into a fraction of one core
      entry_slot->cpu = process_clamp(
          (float)delta * (float)target->core_count / (float)total_delta, 0.0f,
          8.0f);
    }

    entry_slot->cpu_jiffies = cpu_jiffies;

    float resident_kb = (float)resident_pages * (float)page_kb;
    entry_slot->memory =
        target->total_memory_kb ? resident_kb / (float)target->total_memory_kb
                                : 0.0f;

    process_pack_name(name_words[slot], &name_length[slot], name);
  }

  closedir(proc);

  //anything not walked over this sweep has exited, so its slot goes back
  for (int i = 0; i < PROCESS_MAX; i++)
    if (!target->slots[i].seen)
      target->slots[i].pid = 0;

  pthread_mutex_lock(&target->sample_mutex);

  for (int i = 0; i < PROCESS_MAX; i++) {

    target->live[i] = target->slots[i].pid != 0;
    target->cpu[i] = target->slots[i].cpu;
    target->memory[i] = target->slots[i].memory;

    if (target->live[i]) {
      memcpy(target->name[i], name_words[i], sizeof(name_words[i]));
      target->name_length[i] = name_length[i];
    }
  }

  pthread_mutex_unlock(&target->sample_mutex);
}

static void *process_sampler_thread(void *data) {

  Processes *target = data;

  while (target->running) {

    process_sample(target);

    for (int i = 0; i < PROCESS_SLEEP_CHUNKS && target->running; i++)
      usleep(PROCESS_SAMPLE_INTERVAL_US / PROCESS_SLEEP_CHUNKS);
  }

  return NULL;
}

//colour carries memory: small processes sit cool, the hogs burn. log scaled,
//because almost everything on a machine is a few megabytes and a browser is a
//few thousand. busy processes are then washed towards white, so the towers
//that are tall are also the ones that catch the eye and can be read
static void process_set_color(PInstance *tower, float memory_fraction,
                              float busy, u64 total_memory_kb) {

  float megabytes = memory_fraction * (float)total_memory_kb / 1024.0f;

  float hot = log2f(1.0f + megabytes) / log2f(1.0f + PROCESS_MEMORY_FULL_MB);
  hot = process_clamp(hot, 0.0f, 1.0f);

  float red = 0.25f + hot * 0.75f;
  float green = 0.60f - hot * 0.35f;
  float blue = 1.00f - hot * 0.75f;

  float wash = process_clamp(busy, 0.0f, 1.0f);

  tower->color[0] = red + (1.0f - red) * wash;
  tower->color[1] = green + (1.0f - green) * wash;
  tower->color[2] = blue + (1.0f - blue) * wash;
}

//how many towers fit shoulder to shoulder around a ring of this radius
static int process_ring_capacity(float radius) {
  int capacity =
      (int)(PROCESS_TAU * radius / (PROCESS_FOOTPRINT + PROCESS_BLOCK_GAP));
  return capacity < 1 ? 1 : capacity;
}

//every slot gets its ring position once, at startup, and keeps it
static void process_layout(Processes *target) {

  float radius = PROCESS_INNER_RADIUS;
  int ring_count = process_ring_capacity(radius);
  int placed_in_ring = 0;

  for (int i = 0; i < PROCESS_MAX; i++) {

    PInstance tower;
    ZERO(tower);

    float angle = (float)placed_in_ring / (float)ring_count * PROCESS_TAU;

    tower.position[0] = cosf(angle) * radius;
    tower.position[1] = sinf(angle) * radius;
    tower.position[2] = 0;

    tower.scale[0] = PROCESS_FOOTPRINT;
    tower.scale[1] = PROCESS_FOOTPRINT;

    //a slot with nothing in it is a degenerate box, so it rasterises nothing
    tower.scale[2] = 0;

    array_add(&target->towers, &tower);

    placed_in_ring++;

    if (placed_in_ring >= ring_count) {
      radius += PROCESS_FOOTPRINT + PROCESS_RING_GAP;
      ring_count = process_ring_capacity(radius);
      placed_in_ring = 0;
    }
  }
}

void processes_init(Processes *target) {

  target->core_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (target->core_count < 1)
    target->core_count = 1;

  target->total_memory_kb = process_read_total_memory_kb();
  target->previous_total_jiffies = process_read_total_jiffies();

  array_init(&target->towers, sizeof(PInstance), PROCESS_MAX);
  process_layout(target);

  city_create_box(&target->model);

  target->model.vertex_buffer = pe_vk_create_buffer(
      target->model.vertex_array.bytes_size, target->model.vertex_array.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  target->model.index_buffer = pe_vk_create_buffer(
      target->model.index_array.bytes_size, target->model.index_array.data,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  target->instance_buffer = pe_vk_create_buffer(
      target->towers.bytes_size, target->towers.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  pe_vk_create_texture(&target->model.texture,
                       "/usr/libexec/swordfish/images/font.png");

  pe_vk_create_uniform_buffers(&target->model);
  pe_vk_descriptor_pool_create(&target->model);
  pe_vk_create_descriptor_sets(&target->model,
                               pe_vk_descriptor_set_layout_with_texture);
  pe_vk_descriptor_with_image_update(&target->model);

  glm_mat4_identity(target->model.model_mat);
  glm_mat4_copy(target->model.model_mat,
                target->model.uniform_buffer_object.model);

  PCreateShaderInfo process_shader = {
      .transparency = false,
      .out_shader = &target->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/city_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/city_frag.spv",
      .layout = pe_vk_pipeline_layout3};

  pe_vk_create_shader_instanced(&process_shader);

  pthread_mutex_init(&target->sample_mutex, NULL);

  target->running = true;
  pthread_create(&target->sampler, NULL, process_sampler_thread, target);

  LOG("Processes: %i slots, %i cores, %lu kB ram\n", PROCESS_MAX,
      target->core_count, target->total_memory_kb);
}

void processes_draw(Processes *target, VkCommandBuffer *cmd_buffer,
                    u32 image_index) {

  float cpu[PROCESS_MAX];
  float memory[PROCESS_MAX];
  bool live[PROCESS_MAX];

  pthread_mutex_lock(&target->sample_mutex);

  memcpy(cpu, target->cpu, sizeof(cpu));
  memcpy(memory, target->memory, sizeof(memory));
  memcpy(live, target->live, sizeof(live));

  for (int i = 0; i < PROCESS_MAX; i++) {
    if (!live[i])
      continue;

    PInstance *tower = array_get(&target->towers, i);
    memcpy(tower->name, target->name[i], sizeof(tower->name));
    tower->name_length = target->name_length[i];
  }

  pthread_mutex_unlock(&target->sample_mutex);

  for (int i = 0; i < PROCESS_MAX; i++) {

    PInstance *tower = array_get(&target->towers, i);

    if (!live[i]) {
      //collapse rather than jump, so an exit reads as the tower sinking
      target->displayed_cpu[i] = 0.0f;
      target->displayed_memory[i] = 0.0f;
      tower->scale[2] = 0.0f;
      continue;
    }

    target->displayed_cpu[i] +=
        (cpu[i] - target->displayed_cpu[i]) * PROCESS_SMOOTHING;
    target->displayed_memory[i] +=
        (memory[i] - target->displayed_memory[i]) * PROCESS_SMOOTHING;

    float busy =
        process_clamp(target->displayed_cpu[i] / PROCESS_CPU_FULL, 0.0f, 1.0f);

    tower->scale[2] =
        PROCESS_MIN_HEIGHT + busy * (PROCESS_MAX_HEIGHT - PROCESS_MIN_HEIGHT);

    process_set_color(tower, target->displayed_memory[i], busy,
                      target->total_memory_kb);
  }

  //INFO: safe only because pe_vk_draw_frame waits for the queue to go idle
  //before the next frame is recorded. drop that wait and this needs to be
  //gated on the frame fence instead
  pe_vk_update_buffer(&target->instance_buffer, target->towers.data,
                      target->towers.bytes_size);

  glm_mat4_copy(main_camera.view, target->model.uniform_buffer_object.view);
  glm_mat4_copy(main_camera.projection,
                target->model.uniform_buffer_object.projection);

  target->model.uniform_buffer_object.light_position[3] =
      process_elapsed_seconds();

  pe_vk_send_uniform_buffer(&target->model, image_index);

  PDrawModelCommand draw = {.model = &target->model,
                            .command_buffer = *cmd_buffer,
                            .image_index = image_index,
                            .layout = pe_vk_pipeline_layout3};

  pe_vk_draw_model_instanced(&draw, target->instance_buffer.buffer,
                             target->towers.count);
}

void processes_clean(Processes *target) {

  target->running = false;
  pthread_join(target->sampler, NULL);

  pthread_mutex_destroy(&target->sample_mutex);

  vkFreeMemory(vk_device, target->instance_buffer.memory, NULL);

  pe_vk_clean_image(&target->model.texture);

  pe_clean_model(&target->model);
}
