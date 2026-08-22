#include "hud.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <engine/engine2d.h>
#include <engine/log.h>
#include <engine/macros.h>
#include <engine/utils.h>
#include <engine/vertex.h>

#include <engine/renderer/descriptor_set.h>
#include <engine/renderer/draw.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/uniform_buffer.h>
#include <engine/renderer/vk_buffer.h>
#include <engine/renderer/vk_images.h>

#include "processes.h"
#include "swordfish.h"
#include "system_monitor.h"

Hud hud;

//the atlas is a 16x16 grid of 32px cells in a 512px image
#define HUD_ATLAS_CELL 32.0f
#define HUD_ATLAS_SIZE 512.0f

#define HUD_CHAR_SIZE 26.0f
#define HUD_ADVANCE 15.0f
#define HUD_LINE_HEIGHT 30.0f
#define HUD_MARGIN 24.0f

//twice a second, the same rate the samplers run at. rebuilding every frame
//would just retrace identical glyphs
#define HUD_UPDATE_SECONDS 0.5f

static float hud_elapsed_seconds(void) {

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

//one character as four vertices. a character with no glyph to draw collapses
//to a single point so it covers nothing while keeping the index count fixed
static void hud_write_quad(PVertex *out, char character, float x, float y,
                           bool visible) {

  if (!visible) {
    ZERO(out[0]);
    ZERO(out[1]);
    ZERO(out[2]);
    ZERO(out[3]);
    return;
  }

  init_vec3(x, y, 0.5f, out[0].position);
  init_vec3(x, y + HUD_CHAR_SIZE, 0.5f, out[1].position);
  init_vec3(x + HUD_CHAR_SIZE, y + HUD_CHAR_SIZE, 0.5f, out[2].position);
  init_vec3(x + HUD_CHAR_SIZE, y, 0.5f, out[3].position);

  UV uvs[4];
  pe_2d_get_character_uvs(uvs, character, HUD_ATLAS_CELL, HUD_ATLAS_SIZE);

  for (int i = 0; i < 4; i++) {
    out[i].uv[0] = uvs[i].u;
    out[i].uv[1] = uvs[i].v;
  }
}

//lay the string out over the fixed quad set, newlines starting a new row
static void hud_build_geometry(Hud *target) {

  PVertex *vertices = target->model.vertex_array.data;

  float x = HUD_MARGIN;
  float y = HUD_MARGIN;

  int quad = 0;

  for (int i = 0; target->text[i] != '\0' && quad < HUD_MAX_CHARS; i++) {

    char character = target->text[i];

    if (character == '\n') {
      x = HUD_MARGIN;
      y += HUD_LINE_HEIGHT;
      continue;
    }

    hud_write_quad(&vertices[quad * 4], character, x, y, character != ' ');

    x += HUD_ADVANCE;
    quad++;
  }

  //everything past the end of the string draws nothing
  for (; quad < HUD_MAX_CHARS; quad++)
    hud_write_quad(&vertices[quad * 4], ' ', 0, 0, false);

  pe_vk_update_buffer(&target->model.vertex_buffer,
                      target->model.vertex_array.data,
                      target->model.vertex_array.bytes_size);
}

//the packed instance name, read back out
static void hud_unpack_name(const u32 *words, float length, char *out,
                            int out_size) {

  int count = (int)length;
  if (count > out_size - 1)
    count = out_size - 1;

  for (int i = 0; i < count; i++)
    out[i] = (char)((words[i >> 2] >> (8 * (i & 3))) & 0xFF);

  out[count] = '\0';
}

//everything here is render thread state, so none of it needs the samplers'
//locks: displayed_* are only ever touched from this thread
static void hud_build_text(Hud *target) {

  float usage = 0;
  float hottest = 0;

  for (int i = 0; i < system_monitor.core_count; i++) {
    usage += system_monitor.displayed_usage[i];
    if (system_monitor.displayed_temperature[i] > hottest)
      hottest = system_monitor.displayed_temperature[i];
  }

  if (system_monitor.core_count > 0)
    usage /= (float)system_monitor.core_count;

  float total_gb = (float)processes.total_memory_kb / 1048576.0f;
  float used_gb = system_monitor.displayed_memory[0] * total_gb;

  int live = 0;
  int busiest = -1;

  for (int i = 0; i < PROCESS_MAX; i++) {
    if (!processes.live[i])
      continue;

    live++;

    if (busiest < 0 ||
        processes.displayed_cpu[i] > processes.displayed_cpu[busiest])
      busiest = i;
  }

  char busiest_name[PINSTANCE_NAME_MAX + 1] = "-";
  float busiest_cpu = 0;

  if (busiest >= 0) {
    PInstance *tower = array_get(&processes.towers, busiest);
    hud_unpack_name(tower->name, tower->name_length, busiest_name,
                    sizeof(busiest_name));
    busiest_cpu = processes.displayed_cpu[busiest];
  }

  snprintf(target->text, sizeof(target->text),
           "CPU  %3.0f%%   %2.0fC\n"
           "MEM  %.1fG / %.1fG\n"
           "PROC %i\n"
           "TOP  %s %.0f%%",
           usage * 100.0f, hottest, used_gb, total_gb, live, busiest_name,
           busiest_cpu * 100.0f);
}

void hud_init(Hud *target) {

  array_init(&target->model.vertex_array, sizeof(PVertex), HUD_MAX_CHARS * 4);
  array_init(&target->model.index_array, sizeof(u16), HUD_MAX_CHARS * 6);

  //the arrays are filled to capacity up front: the quad count never changes,
  //only whether a quad has any area
  PVertex blank;
  ZERO(blank);

  for (int i = 0; i < HUD_MAX_CHARS * 4; i++)
    array_add(&target->model.vertex_array, &blank);

  for (int quad = 0; quad < HUD_MAX_CHARS; quad++) {
    u16 base = (u16)(quad * 4);
    u16 indices[6] = {base,   (u16)(base + 1), (u16)(base + 2),
                      base,   (u16)(base + 2), (u16)(base + 3)};

    for (int i = 0; i < 6; i++)
      array_add(&target->model.index_array, &indices[i]);
  }

  pe_vk_create_texture(&target->model.texture,
                       "/usr/libexec/swordfish/images/font.png");

  pe_2d_init_vulkan_buffers(&target->model);
  pe_vk_descriptor_with_image_update(&target->model, &main_render_target);

  PCreateShaderInfo hud_shader = {
      .transparency = true,
      .out_shader = &target->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/hud_frag.spv",
      .layout = pe_vk_pipeline_layout3};

  pe_vk_create_shader(&hud_shader);

  target->next_update_seconds = 0;

  LOG("Hud: %i characters\n", HUD_MAX_CHARS);
}

void hud_draw(Hud *target, VkCommandBuffer *cmd_buffer, u32 image_index) {

  float now = hud_elapsed_seconds();

  if (now >= target->next_update_seconds) {
    hud_build_text(target);
    hud_build_geometry(target);
    target->next_update_seconds = now + HUD_UPDATE_SECONDS;
  }

  //geometry is already in pixels, so the model matrix only has to not scale it
  pe_2d_draw(&target->model, image_index, VEC2(0, 0), VEC2(1, 1));

  PDrawModelCommand draw = {.model = &target->model,
                            .command_buffer = *cmd_buffer,
                            .image_index = image_index,
                            .layout = pe_vk_pipeline_layout3};

  pe_vk_draw_model(&draw);
}

void hud_clean(Hud *target) {

  pe_vk_clean_image(&target->model.texture);

  pe_clean_model(&target->model);
}
