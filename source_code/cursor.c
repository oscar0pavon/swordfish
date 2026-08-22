#include "cursor.h"

#include <engine/engine2d.h>
#include <engine/log.h>
#include <engine/utils.h>
#include <engine/vertex.h>

#include <engine/renderer/descriptor_set.h>
#include <engine/renderer/draw.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/uniform_buffer.h>
#include <engine/renderer/vk_buffer.h>
#include <engine/renderer/vulkan.h>

#include "mouse.h"
#include "outputs.h"

Cursor cursor;

//the quad is square so the arrow is not stretched: cursor.frag lays the
//polygon out in uv, and an oblong quad would squash it in one direction. the
//arrow itself covers about 20x30 of these pixels, the rest is the outline's
//room
#define CURSOR_SIZE 32.0f

//every other 2D quad - the client windows and the hud - sits at z 0.5, and the
//depth test is LESS with depth writes on, so an equal depth loses and drawing
//last is not enough: the pointer over a window would be rejected by the
//window's own depth. a nearer z is what puts it on top, and it has to stay
//past the 0.1 near plane pe_2d_init() sets
#define CURSOR_DEPTH 0.2f

//a unit quad, scaled to pixels at draw time. uv runs from the tip at (0,0) to
//(1,1) - the space the arrow is drawn in - and matches the ortho projection's
//y, which counts down from the top of the screen
static void cursor_create_geometry(Cursor *target) {

  array_init(&target->model.vertex_array, sizeof(PVertex), 4);
  array_init(&target->model.index_array, sizeof(u16), 6);

  for (int i = 0; i < 4; i++) {

    float x = (i == 2 || i == 3) ? 1.0f : 0.0f;
    float y = (i == 1 || i == 2) ? 1.0f : 0.0f;

    PVertex corner;
    ZERO(corner);

    init_vec3(x, y, CURSOR_DEPTH, corner.position);
    corner.uv[0] = x;
    corner.uv[1] = y;

    array_add(&target->model.vertex_array, &corner);
  }

  u16 indices[6] = {0, 1, 2, 0, 2, 3};

  for (int i = 0; i < 6; i++)
    array_add(&target->model.index_array, &indices[i]);
}

void cursor_init(Cursor *target) {

  cursor_create_geometry(target);

  //pe_2d_init_vulkan_buffers() without the texture half: the arrow is shaded
  //from uv rather than sampled, so the model has no image to bind and the
  //uniform-buffer-only layout is the one that matches
  target->model.vertex_buffer =
      pe_vk_create_buffer(target->model.vertex_array.bytes_size,
                          target->model.vertex_array.data,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  target->model.index_buffer = pe_vk_create_buffer(
      target->model.index_array.bytes_size, target->model.index_array.data,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  glm_mat4_identity(target->model.model_mat);
  glm_mat4_copy(orthogonal_projection,
                target->model.uniform_buffer_object.projection);

  pe_vk_create_uniform_buffers(&target->model, &main_render_target);
  pe_vk_descriptor_pool_create(&target->model, &main_render_target);

  pe_vk_create_descriptor_sets(&target->model, pe_vk_descriptor_set_layout,
                               &main_render_target);
  pe_vk_descriptor_update(&target->model, &main_render_target);

  PCreateShaderInfo cursor_shader = {
      .transparency = true,
      .out_shader = &target->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/cursor_frag.spv",
      .layout = pe_vk_pipeline_layout_with_descriptors};

  pe_vk_create_shader(&cursor_shader);

  LOG("Cursor: %.0f pixels\n", CURSOR_SIZE);
}

void cursor_draw(Cursor *target, PRenderTarget *render_target,
                 VkCommandBuffer *cmd_buffer, u32 image_index) {

  SwordfishOutput *out =
      &swordfish_outputs[render_target - pe_render_targets];

  //the input thread owns cursor_x/y, so they are taken once here rather than
  //read again further down: a fast move must not put a new x with an old y.
  //cursor_x/y are in the virtual desktop, and this target only draws its own
  //output's slice of it, so the output's origin comes back out. the arrow's
  //tip is the quad's own origin, so the position needs no hotspot subtracted
  //from it
  vec2 position = {(float)(cursor_x - out->x), (float)(cursor_y - out->y)};

  pe_2d_draw_on_target(&target->model, render_target, image_index, position,
                       VEC2(CURSOR_SIZE, CURSOR_SIZE));

  PDrawModelCommand draw = {.model = &target->model,
                            .command_buffer = *cmd_buffer,
                            .image_index = image_index,
                            .layout = pe_vk_pipeline_layout_with_descriptors};

  pe_vk_draw_model(&draw);
}

void cursor_clean(Cursor *target) { pe_clean_model(&target->model); }
