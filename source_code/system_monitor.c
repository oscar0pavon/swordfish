#include "system_monitor.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <engine/array.h>
#include <engine/camera.h>
#include <engine/log.h>
#include <engine/macros.h>
#include <engine/vertex.h>

#include <engine/renderer/descriptor_set.h>
#include <engine/renderer/draw.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/uniform_buffer.h>
#include <engine/renderer/vk_buffer.h>
#include <engine/renderer/vk_images.h>

#include "city.h"
#include "swordfish.h"

SystemMonitor system_monitor;

//the cpu is one square die at the centre of the world with every core inside
//it, so the whole package reads as a single object whatever the core count.
//the city rings it from CITY_INNER_RADIUS outwards
#define MONITOR_FOOTPRINT 2.2f
#define MONITOR_CORE_GAP 0.8f

//the slab the cores stand on, so the die has an edge instead of floating
#define MONITOR_DIE_MARGIN 1.6f
#define MONITOR_DIE_THICKNESS 0.6f

//an idle core still has to be visible, a saturated one still has to sit under
//the skyline rather than through it
#define MONITOR_MIN_HEIGHT 0.6f
#define MONITOR_MAX_HEIGHT 16.0f

//the memory block sits just off one edge of the die, wider towers so it
//reads as a different kind of thing
#define MEMORY_FOOTPRINT 3.0f
#define MEMORY_GAP 1.5f
#define MEMORY_DIE_CLEARANCE 4.0f
#define MEMORY_MIN_HEIGHT 0.6f
#define MEMORY_MAX_HEIGHT 18.0f

//the range the tower colour maps over. below the floor everything reads cool,
//above the ceiling everything reads hot
#define MONITOR_TEMP_MIN 35.0f
#define MONITOR_TEMP_MAX 100.0f

//how far a tower moves toward the latest reading each frame
#define MONITOR_SMOOTHING 0.08f

//half a second between readings. sampling at frame rate would burn the cpu
//this is supposed to be measuring
#define MONITOR_SAMPLE_INTERVAL_US 500000

//the sleep is split so shutdown doesn't have to wait out a whole interval
#define MONITOR_SLEEP_CHUNKS 10

static const char *memory_names[MONITOR_MEMORY_TOWERS] = {"USED", "CACHED",
                                                          "FREE", "SWAP"};

static const float memory_colors[MONITOR_MEMORY_TOWERS][3] = {
    {1.00f, 0.55f, 0.15f}, //used, amber
    {0.30f, 0.55f, 1.00f}, //cached, blue
    {0.25f, 0.85f, 0.45f}, //free, green
    {0.90f, 0.35f, 0.95f}  //swap, magenta
};

//own clock instead of delta_time, which never advances its counter
static float monitor_elapsed_seconds(void) {

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

static float monitor_clamp(float value, float low, float high) {
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

//the per cpu lines of /proc/stat, in order. returns how many were read
static int monitor_read_cpus(CoreSample *out, int max) {

  FILE *file = fopen("/proc/stat", "r");
  if (!file) {
    LOG("Monitor: can't open /proc/stat\n");
    return 0;
  }

  int count = 0;
  char line[512];

  while (fgets(line, sizeof(line), file) != NULL) {

    //the cpu lines come first, anything else means they are done
    if (strncmp(line, "cpu", 3) != 0)
      break;

    if (count >= max)
      break;

    char label[32];
    u64 field[10];
    ZERO(field);

    int read = sscanf(line, "%31s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                      label, &field[0], &field[1], &field[2], &field[3],
                      &field[4], &field[5], &field[6], &field[7], &field[8],
                      &field[9]);

    //label + at least user/nice/system/idle
    if (read < 5)
      continue;

    //"cpu" with no number is the machine wide total, the cores follow it
    if (label[3] == '\0')
      continue;

    //guest and guest_nice are already counted inside user and nice, so the
    //total stops at steal
    u64 total = 0;
    for (int i = 0; i < 8; i++)
      total += field[i];

    out[count].total = total;
    out[count].idle = field[3] + field[4];

    count++;
  }

  fclose(file);
  return count;
}

//the hwmon directory the cpu package reports its temperatures through
static bool monitor_find_coretemp(SystemMonitor *monitor) {

  for (int index = 0; index < 32; index++) {

    char path[256];
    snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%i/name", index);

    FILE *file = fopen(path, "r");
    if (!file)
      continue;

    char name[64];
    name[0] = '\0';

    if (fgets(name, sizeof(name), file) != NULL) {
      name[strcspn(name, "\n")] = '\0';

      if (strcmp(name, "coretemp") == 0 || strcmp(name, "k10temp") == 0) {
        snprintf(monitor->coretemp_path, sizeof(monitor->coretemp_path),
                 "/sys/class/hwmon/hwmon%i", index);
        fclose(file);
        return true;
      }
    }

    fclose(file);
  }

  return false;
}

//coretemp numbers its sensors sparsely and labels them by physical core id,
//so the mapping has to go through each cpu's topology rather than assuming
//tempN belongs to cpuN
static void monitor_map_temperatures(SystemMonitor *monitor) {

  int input_by_core_id[MONITOR_MAX_CORES];
  for (int i = 0; i < MONITOR_MAX_CORES; i++)
    input_by_core_id[i] = -1;

  for (int cpu = 0; cpu < monitor->core_count; cpu++)
    monitor->temperature_input[cpu] = -1;

  if (!monitor_find_coretemp(monitor)) {
    LOG("Monitor: no coretemp sensor, towers stay at the cool end\n");
    return;
  }

  for (int input = 1; input < 128; input++) {

    char path[512];
    snprintf(path, sizeof(path), "%s/temp%i_label", monitor->coretemp_path,
             input);

    FILE *file = fopen(path, "r");
    if (!file)
      continue;

    char label[64];
    int core_id;

    if (fgets(label, sizeof(label), file) != NULL &&
        sscanf(label, "Core %i", &core_id) == 1 && core_id >= 0 &&
        core_id < MONITOR_MAX_CORES)
      input_by_core_id[core_id] = input;

    fclose(file);
  }

  //hyperthread siblings share a physical core, so they share its sensor
  int mapped = 0;

  for (int cpu = 0; cpu < monitor->core_count; cpu++) {

    char path[256];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%i/topology/core_id", cpu);

    FILE *file = fopen(path, "r");
    if (!file)
      continue;

    int core_id;
    if (fscanf(file, "%i", &core_id) == 1 && core_id >= 0 &&
        core_id < MONITOR_MAX_CORES) {
      monitor->temperature_input[cpu] = input_by_core_id[core_id];
      if (monitor->temperature_input[cpu] >= 0)
        mapped++;
    }

    fclose(file);
  }

  LOG("Monitor: %i cpus mapped to %s\n", mapped, monitor->coretemp_path);
}

static float monitor_read_temperature(SystemMonitor *monitor, int input) {

  if (input < 0)
    return 0.0f;

  char path[512];
  snprintf(path, sizeof(path), "%s/temp%i_input", monitor->coretemp_path,
           input);

  FILE *file = fopen(path, "r");
  if (!file)
    return 0.0f;

  int millidegrees = 0;
  int read = fscanf(file, "%i", &millidegrees);

  fclose(file);

  if (read != 1)
    return 0.0f;

  return (float)millidegrees / 1000.0f;
}

//fractions, so the towers stay comparable whatever the machine has installed
static void monitor_read_memory(float *out) {

  FILE *file = fopen("/proc/meminfo", "r");
  if (!file)
    return;

  u64 total = 0, available = 0, cached = 0, buffers = 0;
  u64 swap_total = 0, swap_free = 0;

  char line[256];
  u64 value;

  while (fgets(line, sizeof(line), file) != NULL) {
    if (sscanf(line, "MemTotal: %lu", &value) == 1)
      total = value;
    else if (sscanf(line, "MemAvailable: %lu", &value) == 1)
      available = value;
    else if (sscanf(line, "Cached: %lu", &value) == 1)
      cached = value;
    else if (sscanf(line, "Buffers: %lu", &value) == 1)
      buffers = value;
    else if (sscanf(line, "SwapTotal: %lu", &value) == 1)
      swap_total = value;
    else if (sscanf(line, "SwapFree: %lu", &value) == 1)
      swap_free = value;
  }

  fclose(file);

  if (total == 0)
    return;

  //what MemAvailable leaves out is what is actually spoken for
  out[0] = (float)(total - available) / (float)total;
  out[1] = (float)(cached + buffers) / (float)total;
  out[2] = (float)available / (float)total;
  out[3] = swap_total ? (float)(swap_total - swap_free) / (float)swap_total
                      : 0.0f;
}

//one reading turned into a busy fraction and a temperature per core, plus the
//memory block
static void monitor_sample(SystemMonitor *monitor) {

  CoreSample now[MONITOR_MAX_CORES];
  int count = monitor_read_cpus(now, MONITOR_MAX_CORES);

  if (count > monitor->core_count)
    count = monitor->core_count;

  float usage[MONITOR_MAX_CORES];
  float temperature[MONITOR_MAX_CORES];
  float memory[MONITOR_MEMORY_TOWERS];

  ZERO(memory);

  for (int i = 0; i < count; i++) {

    u64 total_delta = now[i].total - monitor->previous[i].total;
    u64 idle_delta = now[i].idle - monitor->previous[i].idle;

    //a core that logged no jiffies at all keeps whatever it had, dividing
    //through would only produce noise
    usage[i] = (total_delta > 0)
                   ? monitor_clamp((float)(total_delta - idle_delta) /
                                       (float)total_delta,
                                   0.0f, 1.0f)
                   : monitor->usage[i];

    temperature[i] =
        monitor_read_temperature(monitor, monitor->temperature_input[i]);

    monitor->previous[i] = now[i];
  }

  monitor_read_memory(memory);

  pthread_mutex_lock(&monitor->sample_mutex);

  for (int i = 0; i < count; i++) {
    monitor->usage[i] = usage[i];
    monitor->temperature[i] = temperature[i];
  }

  for (int i = 0; i < MONITOR_MEMORY_TOWERS; i++)
    monitor->memory[i] = memory[i];

  pthread_mutex_unlock(&monitor->sample_mutex);
}

static void *monitor_sampler_thread(void *data) {

  SystemMonitor *monitor = data;

  while (monitor->running) {

    monitor_sample(monitor);

    for (int i = 0; i < MONITOR_SLEEP_CHUNKS && monitor->running; i++)
      usleep(MONITOR_SAMPLE_INTERVAL_US / MONITOR_SLEEP_CHUNKS);
  }

  return NULL;
}

//height is what the core is doing, colour is how hot it is doing it. the two
//are independent, so a cool busy core and a hot idle one look different
static void monitor_set_temperature_color(PInstance *tower, float celsius) {

  float hot = monitor_clamp(
      (celsius - MONITOR_TEMP_MIN) / (MONITOR_TEMP_MAX - MONITOR_TEMP_MIN),
      0.0f, 1.0f);

  tower->color[0] = 0.20f + hot * 0.80f;
  tower->color[1] = 0.75f - hot * 0.50f;
  tower->color[2] = 1.00f - hot * 0.90f;
}

//same packing the city uses, 4 characters per uint
static void monitor_set_name(PInstance *tower, const char *name) {

  int length = 0;
  while (name[length] != '\0' && length < PINSTANCE_NAME_MAX)
    length++;

  for (int i = 0; i < length; i++) {
    u32 character = (u32)(unsigned char)name[i];
    tower->name[i >> 2] |= character << (8 * (i & 3));
  }

  tower->name_length = (float)length;
}

//every core inside one square die at the centre, memory just off its near
//edge, and the slab they all stand on as the last instance
static void monitor_layout(SystemMonitor *monitor) {

  int columns = (int)ceilf(sqrtf((float)monitor->core_count));
  if (columns < 1)
    columns = 1;

  int rows = (monitor->core_count + columns - 1) / columns;

  float pitch = MONITOR_FOOTPRINT + MONITOR_CORE_GAP;
  float width = columns * pitch;
  float depth = rows * pitch;

  for (int i = 0; i < monitor->core_count; i++) {

    PInstance tower;
    ZERO(tower);

    int column = i % columns;
    int row = i / columns;

    tower.position[0] = (column + 0.5f) * pitch - width * 0.5f;
    tower.position[1] = (row + 0.5f) * pitch - depth * 0.5f;
    tower.position[2] = MONITOR_DIE_THICKNESS;

    tower.scale[0] = MONITOR_FOOTPRINT;
    tower.scale[1] = MONITOR_FOOTPRINT;
    tower.scale[2] = MONITOR_MIN_HEIGHT;

    monitor_set_temperature_color(&tower, MONITOR_TEMP_MIN);

    //uppercase, the atlas rows the facade shader picks from are the legible
    //ones
    char name[16];
    snprintf(name, sizeof(name), "CPU%i", i);
    monitor_set_name(&tower, name);

    array_add(&monitor->towers, &tower);
  }

  float memory_pitch = MEMORY_FOOTPRINT + MEMORY_GAP;
  float memory_width = MONITOR_MEMORY_TOWERS * memory_pitch;
  float memory_y =
      -(depth * 0.5f + MONITOR_DIE_MARGIN + MEMORY_DIE_CLEARANCE);

  for (int i = 0; i < MONITOR_MEMORY_TOWERS; i++) {

    PInstance tower;
    ZERO(tower);

    tower.position[0] = (i + 0.5f) * memory_pitch - memory_width * 0.5f;
    tower.position[1] = memory_y;
    tower.position[2] = 0;

    tower.scale[0] = MEMORY_FOOTPRINT;
    tower.scale[1] = MEMORY_FOOTPRINT;
    tower.scale[2] = MEMORY_MIN_HEIGHT;

    //memory towers keep a fixed colour, only their height moves
    tower.color[0] = memory_colors[i][0];
    tower.color[1] = memory_colors[i][1];
    tower.color[2] = memory_colors[i][2];

    monitor_set_name(&tower, memory_names[i]);

    array_add(&monitor->towers, &tower);
  }

  //the die last, never updated after this
  PInstance die;
  ZERO(die);

  die.position[0] = 0;
  die.position[1] = 0;
  die.position[2] = 0;

  die.scale[0] = width + MONITOR_DIE_MARGIN * 2.0f;
  die.scale[1] = depth + MONITOR_DIE_MARGIN * 2.0f;
  die.scale[2] = MONITOR_DIE_THICKNESS;

  die.color[0] = 0.16f;
  die.color[1] = 0.28f;
  die.color[2] = 0.24f;

  monitor_set_name(&die, "CPU");

  array_add(&monitor->towers, &die);
}

void system_monitor_init(SystemMonitor *monitor) {

  monitor->core_count =
      monitor_read_cpus(monitor->previous, MONITOR_MAX_CORES);

  if (monitor->core_count == 0) {
    LOG("Monitor: no cpus found, nothing to draw\n");
    return;
  }

  monitor_map_temperatures(monitor);

  //cores, then the memory block, then the die slab
  array_init(&monitor->towers, sizeof(PInstance),
             monitor->core_count + MONITOR_MEMORY_TOWERS + 1);
  monitor_layout(monitor);

  city_create_box(&monitor->model);

  monitor->model.vertex_buffer = pe_vk_create_buffer(
      monitor->model.vertex_array.bytes_size, monitor->model.vertex_array.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  monitor->model.index_buffer = pe_vk_create_buffer(
      monitor->model.index_array.bytes_size, monitor->model.index_array.data,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  //unlike the city's, this one is rewritten every frame
  monitor->instance_buffer = pe_vk_create_buffer(
      monitor->towers.bytes_size, monitor->towers.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  pe_vk_create_texture(&monitor->model.texture,
                       "/usr/libexec/swordfish/images/font.png");

  pe_vk_create_uniform_buffers(&monitor->model);
  pe_vk_descriptor_pool_create(&monitor->model);
  pe_vk_create_descriptor_sets(&monitor->model,
                               pe_vk_descriptor_set_layout_with_texture);
  pe_vk_descriptor_with_image_update(&monitor->model);

  glm_mat4_identity(monitor->model.model_mat);
  glm_mat4_copy(monitor->model.model_mat,
                monitor->model.uniform_buffer_object.model);

  //the towers are the city's boxes with a different instance array, so they
  //run the same facade shader
  PCreateShaderInfo monitor_shader = {
      .transparency = false,
      .out_shader = &monitor->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/city_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/city_frag.spv",
      .layout = pe_vk_pipeline_layout3};

  pe_vk_create_shader_instanced(&monitor_shader);

  pthread_mutex_init(&monitor->sample_mutex, NULL);

  monitor->running = true;
  pthread_create(&monitor->sampler, NULL, monitor_sampler_thread, monitor);

  LOG("Monitor: %i cpus\n", monitor->core_count);
}

void system_monitor_draw(SystemMonitor *monitor, VkCommandBuffer *cmd_buffer,
                         u32 image_index) {

  if (monitor->core_count == 0)
    return;

  float usage[MONITOR_MAX_CORES];
  float temperature[MONITOR_MAX_CORES];
  float memory[MONITOR_MEMORY_TOWERS];

  pthread_mutex_lock(&monitor->sample_mutex);
  memcpy(usage, monitor->usage, sizeof(float) * monitor->core_count);
  memcpy(temperature, monitor->temperature,
         sizeof(float) * monitor->core_count);
  memcpy(memory, monitor->memory, sizeof(memory));
  pthread_mutex_unlock(&monitor->sample_mutex);

  for (int i = 0; i < monitor->core_count; i++) {

    //ease toward the reading, otherwise the towers snap twice a second
    monitor->displayed_usage[i] +=
        (usage[i] - monitor->displayed_usage[i]) * MONITOR_SMOOTHING;
    monitor->displayed_temperature[i] +=
        (temperature[i] - monitor->displayed_temperature[i]) *
        MONITOR_SMOOTHING;

    PInstance *tower = array_get(&monitor->towers, i);

    tower->scale[2] = MONITOR_MIN_HEIGHT +
                      monitor->displayed_usage[i] *
                          (MONITOR_MAX_HEIGHT - MONITOR_MIN_HEIGHT);

    monitor_set_temperature_color(tower, monitor->displayed_temperature[i]);
  }

  for (int i = 0; i < MONITOR_MEMORY_TOWERS; i++) {

    monitor->displayed_memory[i] +=
        (memory[i] - monitor->displayed_memory[i]) * MONITOR_SMOOTHING;

    PInstance *tower = array_get(&monitor->towers, monitor->core_count + i);

    tower->scale[2] = MEMORY_MIN_HEIGHT +
                      monitor->displayed_memory[i] *
                          (MEMORY_MAX_HEIGHT - MEMORY_MIN_HEIGHT);
  }

  //INFO: safe only because pe_vk_draw_frame waits for the queue to go idle
  //before the next frame is recorded. drop that wait and this needs to be
  //gated on the frame fence instead
  pe_vk_update_buffer(&monitor->instance_buffer, monitor->towers.data,
                      monitor->towers.bytes_size);

  glm_mat4_copy(main_camera.view, monitor->model.uniform_buffer_object.view);
  glm_mat4_copy(main_camera.projection,
                monitor->model.uniform_buffer_object.projection);

  //the fragment stage can't see the ubo, so the facade scroll clock rides in
  //the unused light position slot, same as the city
  monitor->model.uniform_buffer_object.light_position[3] =
      monitor_elapsed_seconds();

  pe_vk_send_uniform_buffer(&monitor->model, image_index);

  PDrawModelCommand draw = {.model = &monitor->model,
                            .command_buffer = *cmd_buffer,
                            .image_index = image_index,
                            .layout = pe_vk_pipeline_layout3};

  pe_vk_draw_model_instanced(&draw, monitor->instance_buffer.buffer,
                             monitor->towers.count);
}

void system_monitor_clean(SystemMonitor *monitor) {

  if (monitor->core_count == 0)
    return;

  monitor->running = false;
  pthread_join(monitor->sampler, NULL);

  pthread_mutex_destroy(&monitor->sample_mutex);

  vkFreeMemory(vk_device, monitor->instance_buffer.memory, NULL);

  pe_vk_clean_image(&monitor->model.texture);

  pe_clean_model(&monitor->model);
}
