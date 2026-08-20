#include "surface.h"

#include "compositor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include "dma.h"
#include <engine/array.h>
#include <engine/images.h>
#include <engine/engine2d.h>
#include <engine/renderer/pipeline.h>
#include <engine/renderer/descriptor_set.h>
#include "swordfish.h"
#include <engine/renderer/vk_images.h>
#include "input.h"
#include <pthread.h>

Array tasks_for_draw;

Task *focused_task;

static void surface_damage(WClient *client, WResource *resource,
                           int32_t x, int32_t y, int32_t width,
                           int32_t height) {
  // Store the damaged region information.
  // ...
  printf("Surface damage\n");
}

static void surface_destroy(WClient *client, WResource *resource) {

  //the image is torn down in destroy_surface() instead, under
  //draw_tasks_mutex: freeing it here means freeing a vulkan image the render
  //thread may still be sampling, and the task is not out of tasks_for_draw yet
  wl_resource_destroy(resource);

  printf("Surface destroy\n");
}

//must be called with draw_tasks_mutex held: handle_frame() stores the next
//callback from the compositor thread while this runs on the render thread, and
//a store landing between the send and the NULL either destroys a resource the
//client is still waiting on or drops the new one on the floor. the first is a
//use-after-free that libwayland reports as "Data too big for buffer" and
//answers by disconnecting the client
void send_frame_callback_done(Task *surface){
  WResource *callback = surface->frame_call_resource;

  //cleared first, so nothing can see a resource that is about to be destroyed
  surface->frame_call_resource = NULL;

  wl_callback_send_done(callback, get_current_time_msec());
  wl_resource_destroy(callback);
}

//a client may destroy a wl_buffer whenever it likes - mesa does it when the
//window resizes - and nothing told the task, which went on sending
//wl_buffer.release through the freed resource every frame
static void handle_buffer_destroyed(struct wl_listener *listener, void *data) {

  Task *surface = wl_container_of(listener, surface, buffer_destroy);

  //this is the compositor thread and draw_surfaces() reads the same field on
  //the render thread
  pthread_mutex_lock(&draw_tasks_mutex);

  wl_list_remove(&surface->buffer_destroy.link);
  surface->listening_to_buffer = false;
  surface->buffer_resource = NULL;

  //TODO the PTexture and its vulkan image outlive the wl_buffer that owned
  //them. freeing them here would destroy an image the render thread is still
  //recording with, so for now the surface keeps sampling what it has
  pthread_mutex_unlock(&draw_tasks_mutex);
}

static void stop_listening_to_buffer(Task *surface) {

  if (!surface->listening_to_buffer)
    return;

  wl_list_remove(&surface->buffer_destroy.link);
  surface->listening_to_buffer = false;
}

static void listen_to_buffer(Task *surface, WResource *buffer_resource) {

  stop_listening_to_buffer(surface);

  surface->buffer_destroy.notify = handle_buffer_destroyed;
  wl_resource_add_destroy_listener(buffer_resource, &surface->buffer_destroy);
  surface->listening_to_buffer = true;
}

//the buffer waiting to be handed back is a resource the client may destroy
//before that happens, exactly like the current one
static void handle_old_buffer_destroyed(struct wl_listener *listener,
                                        void *data) {

  Task *surface = wl_container_of(listener, surface, old_buffer_destroy);

  pthread_mutex_lock(&draw_tasks_mutex);

  wl_list_remove(&surface->old_buffer_destroy.link);
  surface->listening_to_old_buffer = false;
  surface->old_buffer_resource = NULL;

  pthread_mutex_unlock(&draw_tasks_mutex);
}

static void stop_listening_to_old_buffer(Task *surface) {

  if (!surface->listening_to_old_buffer)
    return;

  wl_list_remove(&surface->old_buffer_destroy.link);
  surface->listening_to_old_buffer = false;
}

//record that the client is owed this buffer back, without sending it yet
static void owe_release_on(Task *surface, WResource *buffer_resource) {

  //a client that attaches twice inside one frame leaves two owed. the older of
  //the two stopped being the sampled one when the second arrived, and the frame
  //that read it has been through vkQueueWaitIdle by now, so it can go straight
  //back
  if (surface->old_buffer_resource) {
    wl_buffer_send_release(surface->old_buffer_resource);
    stop_listening_to_old_buffer(surface);
  }

  surface->old_buffer_resource = buffer_resource;

  surface->old_buffer_destroy.notify = handle_old_buffer_destroyed;
  wl_resource_add_destroy_listener(buffer_resource,
                                   &surface->old_buffer_destroy);
  surface->listening_to_old_buffer = true;
}

//called from end_frame() on the render thread, after pe_vk_draw_frame() has
//drained the queue. the caller holds lock_wayland() and draw_tasks_mutex
void task_release_old_buffer(Task *surface) {

  if (!surface->old_buffer_resource)
    return;

  wl_buffer_send_release(surface->old_buffer_resource);

  stop_listening_to_old_buffer(surface);
  surface->old_buffer_resource = NULL;
}

//a cursor image is never drawn, so nothing in the render loop will ever
//release its buffer. hand it straight back or the client waits for a buffer it
//is never getting
void mark_surface_as_cursor(Task *task) {

  pthread_mutex_lock(&draw_tasks_mutex);

  task->is_cursor = true;
  task->can_draw = false;

  //set_cursor arrives after the client has already attached and committed the
  //image, so by now the task is usually in the draw list
  array_remove_element(&tasks_for_draw, task);

  if (task->buffer_resource && !task->buffer_released) {
    wl_buffer_send_release(task->buffer_resource);
    task->buffer_released = true;
  }

  pthread_mutex_unlock(&draw_tasks_mutex);

  printf("Surface is a cursor\n");
}

void surface_attach(WClient *client, WResource *resource,
                           WResource *buffer_resource, int32_t x,
                           int32_t y) {

  Task *surface = wl_resource_get_user_data(resource);

  //attaching NULL unmaps the surface. clients do it to their cursor as a way
  //of hiding it, and the user data below is read straight through
  if (!buffer_resource) {
    pthread_mutex_lock(&draw_tasks_mutex);

    //unmapping does not excuse the compositor from handing the buffer back, and
    //the surface stops being drawn here rather than when the release goes out
    if (surface->buffer_resource && !surface->buffer_released)
      owe_release_on(surface, surface->buffer_resource);

    stop_listening_to_buffer(surface);
    surface->buffer_resource = NULL;
    surface->can_draw = false;

    array_remove_element(&tasks_for_draw, surface);
    pthread_mutex_unlock(&draw_tasks_mutex);

    printf("Surface detached\n");
    return;
  }

  pthread_mutex_lock(&draw_tasks_mutex);

  //the buffer being replaced is the one the quad has been sampling every frame
  //since it arrived, so it is only now that the client can have it back - and
  //not even now, because this frame may still be on the gpu
  if (surface->buffer_resource && !surface->buffer_released)
    owe_release_on(surface, surface->buffer_resource);

  surface->buffer_resource = buffer_resource;

  //this buffer has not been handed back yet, whatever was true of the last one
  surface->buffer_released = false;

  listen_to_buffer(surface, buffer_resource);

  pthread_mutex_unlock(&draw_tasks_mutex);

  //a cursor stays out of the draw list however often it is redrawn
  if (surface->is_cursor) {
    wl_buffer_send_release(buffer_resource);
    surface->buffer_released = true;
    return;
  }

  PTexture *image_buffer = wl_resource_get_user_data(buffer_resource);

  printf("Got image with %i %i\n", image_buffer->width, image_buffer->heigth);
  surface->image = image_buffer;
  memcpy(&surface->model.texture, image_buffer, sizeof(PTexture));

  surface->can_draw = true;

  pthread_mutex_lock(&draw_tasks_mutex);
  array_add_pointer(&tasks_for_draw, surface);

  pthread_mutex_unlock(&draw_tasks_mutex);
  

  surface->x = x;
  surface->y = y;

  printf("Surface attached\n");
}



void surface_commit(WClient *client, WResource *resource) {

  Task *surface = wl_resource_get_user_data(resource);




  printf("Surface committed! Ready to draw.\n");
}


void handle_frame(WClient *client, WResource *resource, uint32_t callback_id){

  WResource *callback_resource = 
    wl_resource_create(client, &wl_callback_interface, 1, callback_id);

  if (!callback_resource) {
    wl_client_post_no_memory(client);
    printf("Can't creat frame callback resource\n");
    return;
  }

  Task *surface = wl_resource_get_user_data(resource);

  //the render thread reads this field in end_frame() and destroys what it
  //finds, so the store has to be under the same lock or the two threads race
  //over the same wl_resource
  pthread_mutex_lock(&draw_tasks_mutex);

  //a client asking twice before a frame went out would otherwise leak the
  //first callback and leave it unanswered forever
  if (surface->frame_call_resource)
    send_frame_callback_done(surface);

  surface->frame_call_resource = callback_resource;

  pthread_mutex_unlock(&draw_tasks_mutex);

}

//every request in the interface needs a handler, NULL is dispatched as a call
//and takes the compositor down with the client. these are the ones swordfish
//has nothing to do with: the quad is drawn from the whole buffer at whatever
//size it arrives, so regions, transform and scale change nothing yet
static void surface_set_opaque_region(WClient *client, WResource *resource,
                                      WResource *region) {}

static void surface_set_input_region(WClient *client, WResource *resource,
                                     WResource *region) {}

//since version 2
static void surface_set_buffer_transform(WClient *client, WResource *resource,
                                         int32_t transform) {
  //TODO the quad ignores this and always samples the buffer upright
}

//since version 3
static void surface_set_buffer_scale(WClient *client, WResource *resource,
                                     int32_t scale) {
  //TODO a scale above 1 means the buffer is bigger than the surface
}

//since version 4, damage in buffer coordinates. the whole texture is
//re-uploaded every attach, so like surface_damage there is nothing to record
static void surface_damage_buffer(WClient *client, WResource *resource,
                                  int32_t x, int32_t y, int32_t width,
                                  int32_t height) {}

const struct wl_surface_interface surface_implementation = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = handle_frame, 
    .set_opaque_region = surface_set_opaque_region,
    .set_input_region = surface_set_input_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale = surface_set_buffer_scale,
    .damage_buffer = surface_damage_buffer,
};

static void destroy_surface(WResource *resource) {
  Task *surface = wl_resource_get_user_data(resource);

  //focused_task is a bare global and the keyboard holds its own pointer, so
  //both have to let go before this memory goes away
  forget_task(surface);

  wl_list_remove(&surface->link);

  //draw_surfaces() and end_frame() walk tasks_for_draw on the render thread,
  //so this cannot pull the Task out and free it unsynchronised - a client
  //exiting mid-frame left the renderer drawing freed memory and jumping
  //through a freed pipeline pointer. can_draw_surfaces was meant to cover this
  //and never could: the render side only ever read it behind a comment
  pthread_mutex_lock(&draw_tasks_mutex);

  array_remove_element(&tasks_for_draw, surface);

  //the buffer can outlive the surface that attached it, and the listener lives
  //inside the Task that is about to be freed
  stop_listening_to_buffer(surface);
  stop_listening_to_old_buffer(surface);

  //a surface destroyed before it ever attached a buffer has no image, and
  //pe_vk_clean_image() reads straight through the pointer. every client that
  //shuts down cleanly without drawing came through here and took the
  //compositor with it
  if (surface->image)
    pe_vk_clean_image(surface->image);

  free(surface);

  pthread_mutex_unlock(&draw_tasks_mutex);

  printf("Destroyed surface\n");
}

void create_surface(WClient *client, WResource *resource,
                    uint32_t id) {

  SwordfishCompositor *compositor = wl_resource_get_user_data(resource);

  Task *surface = calloc(1, sizeof(Task));




  pe_2d_create_quad_geometry(&surface->model);

  PCreateShaderInfo quad_shader = {
      .transparency = true,
      .out_shader = &surface->model.shader,
      .vertex_path = "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  pe_vk_create_shader(&quad_shader);


  if (!surface) {
    printf("Can't create wayland surface\n");
    wl_client_post_no_memory(client);
    return;
  }

  //the surface inherits the version the client bound wl_compositor at, so a
  //version 4 client gets a surface that accepts the version 4 requests
  surface->resource = wl_resource_create(client, &wl_surface_interface,
                                         wl_resource_get_version(resource), id);
  if (!surface->resource) {
    free(surface);
    printf("Can't create wayland resource\n");
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(surface->resource, &surface_implementation,
                                 surface,
                                 destroy_surface); // Set the destroy handler

  //focus is taken in get_top_level_implementation() rather than here: a
  //wl_surface is not necessarily a window, and once the seat advertised a
  //pointer the cursor images clients create came through here and took the
  //keyboard away from the window that asked for them

  surface->compositor = compositor;
  wl_list_insert(&compositor->surfaces, &surface->link);
  printf("New surface created with ID %u\n", id);
}
