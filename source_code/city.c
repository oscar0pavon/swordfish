#include "city.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

#include "swordfish.h"

City city;

//array_add does not grow the array, so this is a hard limit
#define CITY_MAX_BUILDINGS 4096

//this engine uses Z as up, so buildings stand on the XY plane and grow
//along +Z. they ring the cpu die at the centre of the world, filling one
//ring evenly before starting the next one further out
//wide enough that a normal length file name fits across the facade
#define CITY_INNER_RADIUS 30.0f
#define CITY_RING_GAP 2.5f
#define CITY_FOOTPRINT 4.5f
#define CITY_BLOCK_GAP 1.0f

#define CITY_TAU 6.28318530f

#define CITY_MIN_HEIGHT 1.5f
#define CITY_MAX_HEIGHT 55.0f

#define CITY_BOX_FACES 6

//own clock instead of delta_time, which never advances its counter
static float city_elapsed_seconds(void) {

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

static float city_clamp(float value, float low, float high) {
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

static int city_count_entries(const char *path) {
  DIR *directory = opendir(path);
  if (!directory)
    return 0;

  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    count++;
  }

  closedir(directory);
  return count;
}

//how many buildings fit shoulder to shoulder around a ring of this radius
static int city_ring_capacity(float radius) {
  int capacity = (int)(CITY_TAU * radius / (CITY_FOOTPRINT + CITY_BLOCK_GAP));
  return capacity < 1 ? 1 : capacity;
}

//directories grow with how much they contain, files with how big they are.
//both on a log scale, otherwise one huge entry flattens the whole street
static float city_building_height(const char *full_path, bool is_directory,
                                  off_t size) {
  if (is_directory) {
    int children = city_count_entries(full_path);
    return CITY_MIN_HEIGHT + log2f(1.0f + (float)children) * 3.5f;
  }

  float kilobytes = (float)size / 1024.0f;
  return CITY_MIN_HEIGHT + log2f(1.0f + kilobytes) * 1.4f;
}

//pack the entry name into the instance, 4 characters per uint. the shader
//unpacks one character per row and runs them down the facade
static void city_set_name(PInstance *building, const char *name) {

  int length = 0;
  while (name[length] != '\0' && length < PINSTANCE_NAME_MAX)
    length++;

  for (int i = 0; i < length; i++) {
    u32 character = (u32)(unsigned char)name[i];
    building->name[i >> 2] |= character << (8 * (i & 3));
  }

  building->name_length = (float)length;
}

static void city_set_color(PInstance *building, bool is_directory,
                           float height) {

  float tall = city_clamp(height / CITY_MAX_HEIGHT, 0.0f, 1.0f);

  if (is_directory) {
    //directories are the cyan towers
    building->color[0] = 0.10f;
    building->color[1] = 0.65f + tall * 0.35f;
    building->color[2] = 1.00f;
  } else {
    //files are paler, and the biggest ones burn magenta
    building->color[0] = 0.55f + tall * 0.45f;
    building->color[1] = 0.80f - tall * 0.55f;
    building->color[2] = 1.00f;
  }
}

//uv runs 0..1 across each face, the vertex shader rescales it to world units
static void city_add_face(Array *vertices, vec3 normal, vec3 corner0,
                          vec3 corner1, vec3 corner2, vec3 corner3) {

  const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  float *corners[4] = {corner0, corner1, corner2, corner3};

  for (int i = 0; i < 4; i++) {
    PVertex vertex;
    ZERO(vertex);

    glm_vec3_copy(corners[i], vertex.position);
    glm_vec3_copy(normal, vertex.normal);

    vertex.uv[0] = uvs[i][0];
    vertex.uv[1] = uvs[i][1];

    array_add(vertices, &vertex);
  }
}

//unit box centred on X and Y, spanning 0..1 on Z so it sits on the ground
void city_create_box(PModel *model) {

  array_init(&model->vertex_array, sizeof(PVertex), CITY_BOX_FACES * 4);
  array_init(&model->index_array, sizeof(u16), CITY_BOX_FACES * 6);

  city_add_face(&model->vertex_array, VEC3(0, -1, 0), VEC3(-0.5f, -0.5f, 0),
                VEC3(0.5f, -0.5f, 0), VEC3(0.5f, -0.5f, 1),
                VEC3(-0.5f, -0.5f, 1));

  city_add_face(&model->vertex_array, VEC3(0, 1, 0), VEC3(-0.5f, 0.5f, 0),
                VEC3(0.5f, 0.5f, 0), VEC3(0.5f, 0.5f, 1), VEC3(-0.5f, 0.5f, 1));

  city_add_face(&model->vertex_array, VEC3(-1, 0, 0), VEC3(-0.5f, -0.5f, 0),
                VEC3(-0.5f, 0.5f, 0), VEC3(-0.5f, 0.5f, 1),
                VEC3(-0.5f, -0.5f, 1));

  city_add_face(&model->vertex_array, VEC3(1, 0, 0), VEC3(0.5f, -0.5f, 0),
                VEC3(0.5f, 0.5f, 0), VEC3(0.5f, 0.5f, 1), VEC3(0.5f, -0.5f, 1));

  //roof
  city_add_face(&model->vertex_array, VEC3(0, 0, 1), VEC3(-0.5f, -0.5f, 1),
                VEC3(0.5f, -0.5f, 1), VEC3(0.5f, 0.5f, 1), VEC3(-0.5f, 0.5f, 1));

  //floor, never seen from the street but keeps the box closed
  city_add_face(&model->vertex_array, VEC3(0, 0, -1), VEC3(-0.5f, -0.5f, 0),
                VEC3(0.5f, -0.5f, 0), VEC3(0.5f, 0.5f, 0), VEC3(-0.5f, 0.5f, 0));

  for (u16 face = 0; face < CITY_BOX_FACES; face++) {
    u16 base = face * 4;
    u16 face_indices[6] = {base, (u16)(base + 1), (u16)(base + 2),
                           base, (u16)(base + 2), (u16)(base + 3)};

    for (int i = 0; i < 6; i++)
      array_add(&model->index_array, &face_indices[i]);
  }
}

//one readdir pass, laying entries out on rings around the centre
static void city_scan(City *target, const char *path) {

  DIR *directory = opendir(path);
  if (!directory) {
    LOG("City: can't open %s\n", path);
    return;
  }

  //the ring has to be shared out evenly, so how many go on it has to be known
  //before the first one is placed
  int remaining = city_count_entries(path);

  float radius = CITY_INNER_RADIUS;
  int ring_capacity = city_ring_capacity(radius);
  int ring_count = (remaining < ring_capacity) ? remaining : ring_capacity;
  int placed_in_ring = 0;

  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {

    if (entry->d_name[0] == '.')
      continue;

    if (target->buildings.count >= CITY_MAX_BUILDINGS) {
      LOG("City: stopping at %i buildings\n", CITY_MAX_BUILDINGS);
      break;
    }

    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

    struct stat entry_info;
    if (stat(full_path, &entry_info) != 0)
      continue;

    bool is_directory = S_ISDIR(entry_info.st_mode);

    float height = city_clamp(
        city_building_height(full_path, is_directory, entry_info.st_size),
        CITY_MIN_HEIGHT, CITY_MAX_HEIGHT);

    PInstance building;
    ZERO(building);

    //spread whatever is on this ring over the whole circle, so the centre
    //stays surrounded even when there are only a handful of entries
    float angle = (ring_count > 0)
                      ? (float)placed_in_ring / (float)ring_count * CITY_TAU
                      : 0.0f;

    building.position[0] = cosf(angle) * radius;
    building.position[1] = sinf(angle) * radius;
    building.position[2] = 0;

    building.scale[0] = CITY_FOOTPRINT;
    building.scale[1] = CITY_FOOTPRINT;
    building.scale[2] = height;

    city_set_color(&building, is_directory, height);
    city_set_name(&building, entry->d_name);

    array_add(&target->buildings, &building);

    //start the next ring further out once this one is full
    placed_in_ring++;

    if (placed_in_ring >= ring_count) {
      remaining -= ring_count;
      radius += CITY_FOOTPRINT + CITY_RING_GAP;
      ring_capacity = city_ring_capacity(radius);
      ring_count = (remaining < ring_capacity) ? remaining : ring_capacity;
      placed_in_ring = 0;
    }
  }

  closedir(directory);

  LOG("City: %i buildings from %s\n", target->buildings.count, path);
}

void city_init(City *target, const char *path) {

  target->path = path;

  array_init(&target->buildings, sizeof(PInstance), CITY_MAX_BUILDINGS);

  city_create_box(&target->model);
  city_scan(target, path);

  if (target->buildings.count == 0) {
    LOG("City: nothing to draw\n");
    return;
  }

  target->model.vertex_buffer = pe_vk_create_buffer(
      target->model.vertex_array.bytes_size, target->model.vertex_array.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  target->model.index_buffer = pe_vk_create_buffer(
      target->model.index_array.bytes_size, target->model.index_array.data,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  //read once per building instead of once per vertex
  target->instance_buffer = pe_vk_create_buffer(
      target->buildings.bytes_size, target->buildings.data,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  //the facades are drawn by sampling glyphs out of the font atlas
  pe_vk_create_texture(&target->model.texture,
                       "/usr/libexec/swordfish/images/font.png");

  pe_vk_create_uniform_buffers(&target->model, &main_render_target);
  pe_vk_descriptor_pool_create(&target->model, &main_render_target);
  pe_vk_create_descriptor_sets(&target->model,
                               pe_vk_descriptor_set_layout_with_texture,
                               &main_render_target);
  pe_vk_descriptor_with_image_update(&target->model, &main_render_target);

  glm_mat4_identity(target->model.model_mat);
  glm_mat4_copy(target->model.model_mat,
                target->model.uniform_buffer_object.model);

  PCreateShaderInfo city_shader = {
      .transparency = false,
      .out_shader = &target->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/city_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/city_frag.spv",
      .layout = pe_vk_pipeline_layout3};

  pe_vk_create_shader_instanced(&city_shader);
}

void city_draw(City *target, VkCommandBuffer *cmd_buffer, u32 image_index) {

  if (target->buildings.count == 0)
    return;

  //the camera moves, so the view matrix has to go up every frame
  glm_mat4_copy(main_camera.view, target->model.uniform_buffer_object.view);
  glm_mat4_copy(main_camera.projection,
                target->model.uniform_buffer_object.projection);

  //the fragment stage can't see the ubo (it is bound vertex only), so the
  //scroll clock rides along in the unused light position slot
  target->model.uniform_buffer_object.light_position[3] =
      city_elapsed_seconds();

  pe_vk_send_uniform_buffer(&target->model, image_index);

  PDrawModelCommand draw = {.model = &target->model,
                            .command_buffer = *cmd_buffer,
                            .image_index = image_index,
                            .layout = pe_vk_pipeline_layout3};

  //the whole skyline, one call
  pe_vk_draw_model_instanced(&draw, target->instance_buffer.buffer,
                             target->buildings.count);
}

void city_clean(City *target) {

  if (target->buildings.count == 0)
    return;

  vkFreeMemory(vk_device, target->instance_buffer.memory, NULL);

  pe_vk_clean_image(&target->model.texture);

  pe_clean_model(&target->model);
}
