#include "swordfish.h"
#include "city.h"
#include "processes.h"
#include "system_monitor.h"
#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "engine/array.h"
#include <engine/model.h>

#include <cglm/cglm.h>
#include <math.h>
#include <time.h>
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

#include <engine/camera.h>
#include <engine/time.h>
#include <engine/utils.h>
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

//the camera swings a few degrees around the centre instead of taking input.
//the scene is a wall display, not something to drive, and the input thread
//grabs devices globally so camera keys would fire while typing elsewhere
#define ORBIT_RADIUS 28.0f
#define ORBIT_HEIGHT 28.0f

//aimed above the die so the ring of buildings behind it stays in frame
#define ORBIT_LOOK_Z 10.0f

//a few degrees either side. a full orbit would swing buildings between the
//camera and the cpu
#define ORBIT_SWING_RADIANS 0.21f
#define ORBIT_PERIOD_SECONDS 40.0f

#define SWORDFISH_TAU 6.28318530f

//the box's side faces only carry correctly wound uvs when they are seen from
//-X, so the swing is centred there. a full orbit would need city_create_box
//to lay the uvs out per face instead
#define ORBIT_BASE_ANGLE (SWORDFISH_TAU * 0.5f)

//own clock instead of delta_time, which never advances its counter
static float swordfish_elapsed_seconds(void) {

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

static void swordfish_update_camera(void) {

  float seconds = swordfish_elapsed_seconds();

  //a sine already eases both ends, so the swing never reverses with a snap
  float angle = ORBIT_BASE_ANGLE +
                ORBIT_SWING_RADIANS *
                    sinf(seconds * SWORDFISH_TAU / ORBIT_PERIOD_SECONDS);

  init_vec3(cosf(angle) * ORBIT_RADIUS, sinf(angle) * ORBIT_RADIUS,
            ORBIT_HEIGHT, main_camera.position);

  pe_camera_look_at(&main_camera, VEC3(0, 0, ORBIT_LOOK_Z));
}

void swordfish_draw_scene(VkCommandBuffer *cmd_buffer, uint32_t index){

  //before anything copies the view matrix out of main_camera
  swordfish_update_camera();

  //the running processes as a city ringing the die, one draw call for all
  //of them. swap this for city_draw to get the directory instead
  processes_draw(&processes, cmd_buffer, index);

  //the cpus as a row of towers down the middle of that street
  system_monitor_draw(&system_monitor, cmd_buffer, index);

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

  system_monitor_clean(&system_monitor);

  processes_clean(&processes);

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

  //the process table, drawn as the city around the cpu. city_init(&city,
  //".") puts the directory there instead, the two share the ring
  processes_init(&processes);

  system_monitor_init(&system_monitor);





}
