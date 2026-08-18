#include "system_monitor.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
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

SystemMonitor system_monitor;

//two rows of towers running down the middle of the road. the city keeps its
//buildings outside CITY_STREET_WIDTH, so the median is free and sits straight
//in the camera's line of sight
#define MONITOR_ROWS 2
#define MONITOR_ROW_OFFSET 1.6f
#define MONITOR_FOOTPRINT 1.8f
#define MONITOR_COLUMN_GAP 1.2f
#define MONITOR_START_X 3.0f

//an idle core still has to be visible, a saturated one still has to sit under
//the skyline rather than through it
#define MONITOR_MIN_HEIGHT 0.6f
#define MONITOR_MAX_HEIGHT 16.0f

//how far a tower moves toward the latest reading each frame
#define MONITOR_SMOOTHING 0.08f

//half a second between readings. sampling at frame rate would burn the cpu
//this is supposed to be measuring
#define MONITOR_SAMPLE_INTERVAL_US 500000

//the sleep is split so shutdown doesn't have to wait out a whole interval
#define MONITOR_SLEEP_CHUNKS 10

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

//one reading turned into a busy fraction per core
static void monitor_sample(SystemMonitor *monitor) {

  CoreSample now[MONITOR_MAX_CORES];
  int count = monitor_read_cpus(now, MONITOR_MAX_CORES);

  if (count > monitor->core_count)
    count = monitor->core_count;

  for (int i = 0; i < count; i++) {

    u64 total_delta = now[i].total - monitor->previous[i].total;
    u64 idle_delta = now[i].idle - monitor->previous[i].idle;

    //a core that logged no jiffies at all keeps whatever it had, dividing
    //through would only produce noise
    if (total_delta > 0) {
      float busy = (float)(total_delta - idle_delta) / (float)total_delta;

      pthread_mutex_lock(&monitor->usage_mutex);
      monitor->usage[i] = monitor_clamp(busy, 0.0f, 1.0f);
      pthread_mutex_unlock(&monitor->usage_mutex);
    }

    monitor->previous[i] = now[i];
  }
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

//cold cores sit green, a saturated one burns red
static void monitor_set_color(PInstance *tower, float usage) {
  tower->color[0] = 0.15f + usage * 0.85f;
  tower->color[1] = 0.90f - usage * 0.65f;
  tower->color[2] = 0.35f - usage * 0.30f;
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

//hyperthread siblings are consecutive in /proc/stat, so filling the rows
//first puts each pair side by side across the median
static void monitor_layout(SystemMonitor *monitor) {

  for (int i = 0; i < monitor->core_count; i++) {

    PInstance tower;
    ZERO(tower);

    int row = i % MONITOR_ROWS;
    int column = i / MONITOR_ROWS;

    float side_sign = (row == 0) ? -1.0f : 1.0f;

    tower.position[0] =
        MONITOR_START_X + column * (MONITOR_FOOTPRINT + MONITOR_COLUMN_GAP);
    tower.position[1] = side_sign * MONITOR_ROW_OFFSET;
    tower.position[2] = 0;

    tower.scale[0] = MONITOR_FOOTPRINT;
    tower.scale[1] = MONITOR_FOOTPRINT;
    tower.scale[2] = MONITOR_MIN_HEIGHT;

    monitor_set_color(&tower, 0.0f);

    //uppercase, the atlas rows the facade shader picks from are the legible
    //ones
    char name[16];
    snprintf(name, sizeof(name), "CPU%i", i);
    monitor_set_name(&tower, name);

    array_add(&monitor->towers, &tower);
  }
}

void system_monitor_init(SystemMonitor *monitor) {

  monitor->core_count =
      monitor_read_cpus(monitor->previous, MONITOR_MAX_CORES);

  if (monitor->core_count == 0) {
    LOG("Monitor: no cpus found, nothing to draw\n");
    return;
  }

  array_init(&monitor->towers, sizeof(PInstance), monitor->core_count);
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

  pthread_mutex_init(&monitor->usage_mutex, NULL);

  monitor->running = true;
  pthread_create(&monitor->sampler, NULL, monitor_sampler_thread, monitor);

  LOG("Monitor: %i cpus\n", monitor->core_count);
}

void system_monitor_draw(SystemMonitor *monitor, VkCommandBuffer *cmd_buffer,
                         u32 image_index) {

  if (monitor->core_count == 0)
    return;

  float usage[MONITOR_MAX_CORES];

  pthread_mutex_lock(&monitor->usage_mutex);
  memcpy(usage, monitor->usage, sizeof(float) * monitor->core_count);
  pthread_mutex_unlock(&monitor->usage_mutex);

  for (int i = 0; i < monitor->core_count; i++) {

    //ease toward the reading, otherwise the towers snap twice a second
    monitor->displayed[i] +=
        (usage[i] - monitor->displayed[i]) * MONITOR_SMOOTHING;

    PInstance *tower = array_get(&monitor->towers, i);

    tower->scale[2] = MONITOR_MIN_HEIGHT +
                      monitor->displayed[i] *
                          (MONITOR_MAX_HEIGHT - MONITOR_MIN_HEIGHT);

    monitor_set_color(tower, monitor->displayed[i]);
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

  pthread_mutex_destroy(&monitor->usage_mutex);

  vkFreeMemory(vk_device, monitor->instance_buffer.memory, NULL);

  pe_vk_clean_image(&monitor->model.texture);

  pe_clean_model(&monitor->model);
}
