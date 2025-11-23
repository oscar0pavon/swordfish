#include "swordfish.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "engine/array.h"
#include <engine/model.h>

#include <cglm/cglm.h>
#include "renderer/pipeline.h"
#include <engine/engine2d.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>

#include <cglm/cglm.h>

#include "renderer/uniform_buffer.h"

#include "renderer/descriptor_set.h"
#include "renderer/draw.h"
#include "renderer/vk_images.h"
#include "renderer/vulkan.h"

#include <engine/time.h>
#include <wayland-server-core.h>

#include <pthread.h>

pthread_mutex_t draw_tasks_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t focus_task_mutex = PTHREAD_MUTEX_INITIALIZER;

bool is_drm_rendering = false;
bool can_draw_surfaces = true;

PModel main_cube;
PModel secondary_cube;
PModel background;

PModel text_model;

Camera main_camera;

bool finished_build = false;

float left_position_z = -6;
bool loop_animation = false;

bool main_cube_start_scale = true;

void init_secodary_cube(PModel* model){
   
  mat4 identity;
  glm_mat4_identity(identity);
  glm_translate(identity, VEC3(0,3,-1));
  glm_translate(identity, VEC3(0,0,-3));

  glm_mat4_copy(identity, model->model_mat);
  
}

void sworfish_set_secondary_cube_position(PModel* model, uint32_t image_index){

  //this is the far the cube can reach
  float velocity = 0.0002f*delta_time;
  if(loop_animation){
    init_secodary_cube(model);
    left_position_z = -6;
    loop_animation = false;
  }
  if(left_position_z < -0.6){

    glm_translate(model->model_mat, VEC3(0,0,velocity));
    left_position_z += velocity;
  }else{
    loop_animation = true;
  }
  mat4 final_pisition;
  glm_mat4_mul(main_cube.model_mat, model->model_mat, final_pisition);

  
  //glm_mat4_copy(model->model_mat, model->uniform_buffer_object.model);
  glm_mat4_copy(final_pisition, model->uniform_buffer_object.model);

}

void draw_textured_model(PModel* model, VkCommandBuffer* cmd_buffer, u32 index){

  pe_vk_send_uniform_buffer(model, index);
  pe_vk_descriptor_with_image_update(model);

  PDrawModelCommand draw = {
    .model = model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };

  pe_vk_draw_model(&draw);
}


void draw_surface(Task* surface, VkCommandBuffer *cmd_buffer, uint32_t index){

  pe_2d_draw(&surface->model, index, VEC2(0,0), VEC2(surface->image->width,surface->image->heigth));

  pe_vk_descriptor_with_image_update(&surface->model);//TODO

  PDrawModelCommand draw = {
    .model = &surface->model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };
  pe_vk_draw_model(&draw);

}

void end_frame() {

  pthread_mutex_lock(&draw_tasks_mutex);
  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *surface = array_get_pointer(&tasks_for_draw, i);
    if (surface->frame_call_resource != NULL)
      send_frame_callback_done(surface);
  }


  pthread_mutex_unlock(&draw_tasks_mutex);

  wl_display_flush_clients(compositor.display);
  array_clean(&tasks_for_draw);
}

void draw_surfaces(VkCommandBuffer *command, uint32_t index) {

  pthread_mutex_lock(&draw_tasks_mutex);

  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *task = array_get_pointer(&tasks_for_draw, i);
    if (task->can_draw) {
      draw_surface(task, command, index);
      wl_buffer_send_release(task->buffer_resource);
    }
  }

  pthread_mutex_unlock(&draw_tasks_mutex);
}

void swordfish_draw_scene(VkCommandBuffer *cmd_buffer, uint32_t index){


  //main cube
  swordfish_update_main_cube(&main_cube, index);
  pe_vk_descriptor_update(&main_cube);

  PDrawModelCommand draw_cube = {
    .model = &main_cube,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout_with_descriptors
  };
  pe_vk_draw_model(&draw_cube);

  //secondary_cube
  sworfish_set_secondary_cube_position(&secondary_cube, index);
  draw_textured_model(&secondary_cube, cmd_buffer, index);
 
  glm_mat4_identity(background.uniform_buffer_object.model);
  draw_textured_model(&background, cmd_buffer, index);

  //quad
  // pe_2d_draw(&text_model, index, VEC2(0,0), VEC2(1,1));
  //
  // pe_vk_descriptor_update(&text_model);
  //
  // pe_vk_descriptor_with_image_update(&text_model);

  // PDrawModelCommand draw_quad = {
  //   .model = &text_model,
  //   .command_buffer = *cmd_buffer,
  //   .image_index = index,
  //   .layout = pe_vk_pipeline_layout3
  // };
  // pe_vk_draw_model(&draw_quad);

  //we need to sync with compositor
  //if(can_draw_surfaces)
  draw_surfaces(cmd_buffer, index);
}


//INFO this where you update the position of the model or camera
void swordfish_update_main_cube(PModel *model, uint32_t image_index) {

  if (finished_build == false) {

    glm_rotate(model->model_mat, -0.0001f*delta_time, VEC3(0, 0, 1));
    glm_mat4_copy(model->model_mat, model->uniform_buffer_object.model);
  } else {
    mat4 identity;
    glm_mat4_identity(identity);
    float scale_value = 0.6f;
    vec3 scale = {scale_value, scale_value, scale_value};
    glm_scale(identity, scale);
    glm_mul(model->model_mat, identity, model->uniform_buffer_object.model);
  }

  // pawn_ubo.projection[1][1] *= -1; //TODO maybe we need to do this

  pe_vk_send_uniform_buffer(model, image_index);

}

// INFO first load the texture
void load_textured_model(PModel* model, const char* path){
  pe_vk_load_model(model, path);
  pe_vk_create_descriptor_sets(model,pe_vk_descriptor_set_layout_with_texture);
  //pe_vk_descriptor_with_image_update(model);
}

void clean_swordfish(){

  pe_vk_clean_image(&secondary_cube.texture);
  pe_vk_clean_image(&text_model.texture);
  pe_vk_clean_image(&background.texture);


  pe_clean_model(&secondary_cube);
  pe_clean_model(&text_model);
  background.shader.cleaned = true;
  pe_clean_model(&background);
  pe_clean_model(&main_cube);

}

void swordfish_init(){


  pe_vk_create_texture(&secondary_cube.texture, "/usr/libexec/swordfish/images/bits.png");
  pe_vk_create_texture(&text_model.texture, "/usr/libexec/swordfish/images/font.png");
  pe_vk_create_texture(&background.texture, "/usr/libexec/swordfish/images/background1.png");

  pe_vk_load_model(&main_cube, "/usr/libexec/swordfish/models/wireframe_cube.glb");
  pe_vk_create_descriptor_sets(&main_cube,pe_vk_descriptor_set_layout);
  pe_vk_descriptor_update(&main_cube);
 
  load_textured_model(&secondary_cube,"/usr/libexec/swordfish/models/secondary_cube.glb");
  load_textured_model(&background,"/usr/libexec/swordfish/models/background.glb");


  PCreateShaderInfo main_cube_shader = {
      .transparency = false,
      .out_shader = &main_cube.shader, 
      .vertex_path = "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/red_frag.spv",
      .layout = pe_vk_pipeline_layout_with_descriptors
  };


  pe_vk_create_shader(&main_cube_shader);


  PCreateShaderInfo secondary_cube_shader = {
      .transparency = false,
      .out_shader = &secondary_cube.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  
  pe_vk_create_shader(&secondary_cube_shader);

  background.shader = secondary_cube.shader;

  init_secodary_cube(&secondary_cube);

  pe_2d_init();
  //pe_2d_create_quad_geometry(&quad_model);
  pe_2d_create_text_geometry(&text_model,"main.o input.o window.o", 24);

  PCreateShaderInfo quad_shader = {
      .transparency = true,
      .out_shader = &text_model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  pe_vk_create_shader(&quad_shader);





}
