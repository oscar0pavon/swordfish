#include "swordfish.h"
#include "city.h"
#include "cursor.h"
#include "hud.h"
#include "processes.h"
#include "system_monitor.h"
#include "compositor/compositor.h"
#include "compositor/retire.h"
#include "compositor/shared_memory.h"
#include "compositor/surface.h"
#include <engine/renderer/sync.h>
#include <engine/array.h>
#include <engine/model.h>

#include <cglm/cglm.h>
#include <math.h>
#include <time.h>
#include <engine/renderer/pipeline.h>
#include <engine/engine2d.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>

#include <cglm/cglm.h>

#include <engine/renderer/uniform_buffer.h>

#include <engine/renderer/descriptor_set.h>
#include <engine/renderer/draw.h>
#include <engine/renderer/vk_images.h>
#include <engine/renderer/vulkan.h>

#include <engine/camera.h>
#include <engine/time.h>
#include <engine/utils.h>
#include <wayland-server-core.h>

#include <pthread.h>

pthread_mutex_t draw_tasks_mutex = PTHREAD_MUTEX_INITIALIZER;

bool can_draw_surfaces = true;

PModel text_model;

bool finished_build = false;

void draw_surface(Task* surface, VkCommandBuffer *cmd_buffer, uint32_t index){

  //the cell the layout gave this window. the buffer is stretched into it
  //rather than drawn at its own size, so the tiling has no hole in it during
  //the frame or two between the configure and the client repainting at the new
  //size. a surface the layout has not reached - one that never became a
  //toplevel - keeps the old behaviour and is drawn at the corner
  vec2 position = {surface->tile_x, surface->tile_y};
  vec2 size = {surface->tile_width, surface->tile_height};

  if (surface->tile_width == 0) {
    position[0] = 0;
    position[1] = 0;
    size[0] = surface->image->width;
    size[1] = surface->image->heigth;
  }

  pe_2d_draw(&surface->model, index, position, size);

  pe_vk_descriptor_with_image_update(&surface->model, &main_render_target);//TODO

  PDrawModelCommand draw = {
    .model = &surface->model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };
  pe_vk_draw_model(&draw);

}

void end_frame() {

  //the compositor thread is sending to the same clients out of its event loop.
  //lock_wayland() before draw_tasks_mutex, never the other way round: a
  //request handler already holds the wayland lock when it takes this one
  lock_wayland();
  pthread_mutex_lock(&draw_tasks_mutex);

  //vulkan objects whose wl_buffer the client destroyed while a frame was
  //still using them. retire.c counts frames in flight, so only what no frame
  //can still be reading is destroyed here
  retire_collect();

  //an shm upload rewrites an image a frame in flight may still be sampling,
  //and the single-time commands it is made of carry no barrier against that.
  //pe_vk_draw_frame() used to end in vkQueueWaitIdle() and made every frame
  //safe for free; now the wait is paid explicitly, and only on the frames
  //where an shm client actually redrew
  bool any_upload = false;
  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *surface = array_get_pointer(&tasks_for_draw, i);
    if (surface->client_buffer &&
        surface->client_buffer->type == CLIENT_BUFFER_SHARED_MEMORY &&
        surface->client_buffer->needs_upload) {
      any_upload = true;
      break;
    }
  }

  //INFO the fences moved onto PRenderTarget with multimonitor - wait on every
  //target's, since an shm upload has no way to know which one last drew the
  //surface it is about to overwrite
  if (any_upload)
    for (u32 t = 0; t < pe_render_targets_count; t++)
      vkWaitForFences(vk_device, PE_VK_FRAMES_IN_FLIGHT,
                      pe_render_targets[t].fence_in_flight, VK_TRUE,
                      UINT64_MAX);

  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *surface = array_get_pointer(&tasks_for_draw, i);
    if (surface->frame_call_resource != NULL)
      send_frame_callback_done(surface);

    //an shm client's pixels are only its own memory until they are copied, and
    //the copy is a queue submit - end_frame() is the one point in the frame
    //where the render thread owns the queue and is not recording. it lands in
    //the next frame rather than this one, which is a frame of latency shm pays
    //and dmabuf does not
    task_upload_shared_memory(surface);

    //any buffer the client replaced goes back once the gpu is provably past
    //the last frame that sampled it - task_release_old_buffer() counts the
    //frames itself now that pe_vk_draw_frame() no longer drains the queue
    task_release_old_buffer(surface);
  }


  pthread_mutex_unlock(&draw_tasks_mutex);

  wl_display_flush_clients(compositor.display);

  unlock_wayland();
  //array_clean(&tasks_for_draw);
}

void draw_surfaces(VkCommandBuffer *command, uint32_t index) {

  lock_wayland();
  pthread_mutex_lock(&draw_tasks_mutex);

  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *task = array_get_pointer(&tasks_for_draw, i);
    if (task->can_draw) {
      draw_surface(task, command, index);

      //no release goes out here. a dmabuf buffer is sampled straight out of the
      //client's memory, and this quad goes on sampling this one every frame
      //until the client attaches another - so telling the client it is free the
      //first time it is drawn hands back a buffer that is still on screen. the
      //client then picks it as its next render target and the frame that lands
      //mid-repaint shows the clear rather than the content. task_release_old_buffer()
      //in end_frame() is where it is answered instead
    }
  }

  pthread_mutex_unlock(&draw_tasks_mutex);
  unlock_wayland();
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

void swordfish_draw_scene(PRenderTarget *target, VkCommandBuffer *cmd_buffer, uint32_t index){

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

  //flat overlay last, so the numbers sit over the scene
  hud_draw(&hud, cmd_buffer, index);

  //and the pointer over that. no client has ever handed over a cursor image -
  //set_cursor keeps the surface out of the draw list - so this arrow is the
  //only pointer there is
  cursor_draw(&cursor, cmd_buffer, index);
}


void clean_swordfish(){

  system_monitor_clean(&system_monitor);

  hud_clean(&hud);

  cursor_clean(&cursor);

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

  hud_init(&hud);

  //after pe_2d_init(), which is what fills in the ortho projection it copies
  cursor_init(&cursor);





}
