#include "swordfish.h"
#include "cursor.h"
#include "compositor.h"
#include "retire.h"
#include "shared_memory.h"
#include "surface.h"
#include "mouse.h"
#include "outputs.h"
#include <engine/renderer/sync.h>
#include <engine/array.h>
#include <engine/model.h>

#include <cglm/cglm.h>
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

#include <wayland-server-core.h>

#include <pthread.h>

pthread_mutex_t draw_tasks_mutex = PTHREAD_MUTEX_INITIALIZER;

bool can_draw_surfaces = true;

void draw_surface(Task* surface, PRenderTarget *render_target,
                  VkCommandBuffer *cmd_buffer, uint32_t index){

  SwordfishOutput *out =
      &swordfish_outputs[render_target - pe_render_targets];

  //the cell the layout gave this window, in the virtual desktop - the
  //output's own origin comes back out since this target only draws its own
  //slice of it. the buffer is stretched into it rather than drawn at its own
  //size, so the tiling has no hole in it during the frame or two between the
  //configure and the client repainting at the new size. a surface the layout
  //has not reached - one that never became a toplevel - keeps the old
  //behaviour and is drawn at the corner
  vec2 position = {surface->tile_x - out->x, surface->tile_y - out->y};
  vec2 size = {surface->tile_width, surface->tile_height};

  if (surface->tile_width == 0) {
    position[0] = 0;
    position[1] = 0;
    size[0] = surface->image->width;
    size[1] = surface->image->heigth;
  }

  pe_2d_draw_on_target(&surface->model, render_target, index, position, size);

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

void draw_surfaces(PRenderTarget *render_target, VkCommandBuffer *command,
                   uint32_t index) {

  int output_index = render_target - pe_render_targets;

  lock_wayland();
  pthread_mutex_lock(&draw_tasks_mutex);

  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *task = array_get_pointer(&tasks_for_draw, i);
    if (task->can_draw && task->output_index == output_index) {
      draw_surface(task, render_target, command, index);

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

//the 3D scene this used to draw - the cpu die, the process ring and the hud -
//is its own program now: 3dtop, an ordinary wayland client. it was never
//visible from behind the tiling anyway, and as a client it gets composited
//like anything else. what is left here is the compositor's own drawing
void swordfish_draw_scene(PRenderTarget *target, VkCommandBuffer *cmd_buffer, uint32_t index){

  int output_index = target - pe_render_targets;

  draw_surfaces(target, cmd_buffer, index);

  //the pointer over the windows, on whichever output it is actually over. no
  //client has ever handed over a cursor image - set_cursor keeps the surface
  //out of the draw list - so this arrow is the only pointer there is
  if (swordfish_output_index_at(cursor_x) == output_index)
    cursor_draw(&cursor, target, cmd_buffer, index);
}

void clean_swordfish(){

  cursor_clean(&cursor);

}

void swordfish_init(){

  //fills in the ortho projection cursor_init() copies, so it comes first
  pe_2d_init();

  cursor_init(&cursor);

}
