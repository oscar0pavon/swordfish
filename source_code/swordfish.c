#include "swordfish.h"
#include "city.h"
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

PModel text_model;

Camera main_camera;

bool finished_build = false;

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
  //array_clean(&tasks_for_draw);
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

  //the directory as a street of towers, one draw call for all of it
  city_draw(&city, cmd_buffer, index);

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


void clean_swordfish(){

  city_clean(&city);

  pe_vk_clean_image(&text_model.texture);

  pe_clean_model(&text_model);

}

void swordfish_init(){


  pe_vk_create_texture(&text_model.texture, "/usr/libexec/swordfish/images/font.png");

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

  //the directory swordfish was launched from, drawn as a city
  city_init(&city, ".");





}
