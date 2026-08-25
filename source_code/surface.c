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
#include "sword.h"
#include <engine/renderer/vk_images.h>
#include "input.h"
#include "layout.h"
#include "retire.h"
#include "popup.h"
#include "shared_memory.h"
#include "subcompositor.h"
#include "top_level.h"
#include "log.h"

Array tasks_for_draw;

Task *focused_task;

static void surface_damage(WClient *client, WResource *resource,
                           int32_t x, int32_t y, int32_t width,
                           int32_t height) {
  // Store the damaged region information.
  // ...
  log_debug("Surface damage");
}

static void surface_destroy(WClient *client, WResource *resource) {

  //the image is torn down in destroy_surface() instead: freeing it here means
  //freeing a vulkan image a frame still in flight on the gpu may be sampling,
  //and the task is not out of tasks_for_draw yet
  wl_resource_destroy(resource);

  log_info("Surface destroy");
}

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

  wl_list_remove(&surface->buffer_destroy.link);
  surface->listening_to_buffer = false;
  surface->buffer_resource = NULL;

  //the ClientBuffer is freed with the resource, and the upload path would read
  //it on the next frame
  ClientBuffer *buffer = surface->client_buffer;
  surface->client_buffer = NULL;

  //the image belongs to the wl_buffer and is queued for destruction with it -
  //shm and dma alike go through the retire list now - so the quad has to stop
  //sampling it, whichever protocol it came in on. the dma side used to keep
  //drawing "what it had", which was a PTexture the buffer's destructor had
  //already freed
  if (buffer) {
    surface->can_draw = false;
    surface->image = NULL;
    array_remove_element(&tasks_for_draw, surface);
  }
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

  wl_list_remove(&surface->old_buffer_destroy.link);
  surface->listening_to_old_buffer = false;
  surface->old_buffer_resource = NULL;
}

static void stop_listening_to_old_buffer(Task *surface) {

  if (!surface->listening_to_old_buffer)
    return;

  wl_list_remove(&surface->old_buffer_destroy.link);
  surface->listening_to_old_buffer = false;
}

//record that the client is owed this buffer back, without sending it yet
static void owe_release_on(Task *surface, WResource *buffer_resource) {

  //a client that attaches twice inside one frame leaves two owed, and there is
  //only room to remember one. the older goes straight back - with frames in
  //flight that can be a frame early, but a double attach means the client has
  //already stopped caring what either buffer shows
  if (surface->old_buffer_resource) {
    wl_buffer_send_release(surface->old_buffer_resource);
    stop_listening_to_old_buffer(surface);
  }

  surface->old_buffer_resource = buffer_resource;

  //the frame being drawn while this lands is the last one that can have
  //recorded a sample of the old buffer, and task_release_old_buffer() holds
  //the release until the gpu is provably past it - pe_vk_draw_frame() keeps
  //PE_VK_FRAMES_IN_FLIGHT frames going now and no longer drains the queue, so
  //"the frame that read it is finished" became a counted condition
  surface->old_buffer_frame = retire_frame_number();

  surface->old_buffer_destroy.notify = handle_old_buffer_destroyed;
  wl_resource_add_destroy_listener(buffer_resource,
                                   &surface->old_buffer_destroy);
  surface->listening_to_old_buffer = true;
}

//called from end_frame(), the one point in the frame loop where nothing is
//recording a command buffer
void task_release_old_buffer(Task *surface) {

  if (!surface->old_buffer_resource)
    return;

  //not owed yet: a frame that sampled this buffer can still be on the gpu.
  //releasing it there is the flicker bug all over again - the client takes the
  //buffer as free, mesa picks it as the next render target, and the repaint
  //shows the clear
  if (!retire_frame_is_finished(surface->old_buffer_frame))
    return;

  wl_buffer_send_release(surface->old_buffer_resource);

  stop_listening_to_old_buffer(surface);
  surface->old_buffer_resource = NULL;
}

//a cursor image is never drawn, so nothing in the render loop will ever
//release its buffer. hand it straight back or the client waits for a buffer it
//is never getting
void mark_surface_as_cursor(Task *task) {

  task->is_cursor = true;
  task->can_draw = false;

  //set_cursor arrives after the client has already attached and committed the
  //image, so by now the task is usually in the draw list
  array_remove_element(&tasks_for_draw, task);

  if (task->buffer_resource && !task->buffer_released) {
    wl_buffer_send_release(task->buffer_resource);
    task->buffer_released = true;
  }

  log_info("Surface is a cursor");
}

void surface_attach(WClient *client, WResource *resource,
                           WResource *buffer_resource, int32_t x,
                           int32_t y) {

  Task *surface = wl_resource_get_user_data(resource);

  //attaching NULL unmaps the surface. clients do it to their cursor as a way
  //of hiding it, and the user data below is read straight through
  if (!buffer_resource) {
    //unmapping does not excuse the compositor from handing the buffer back, and
    //the surface stops being drawn here rather than when the release goes out
    if (surface->buffer_resource && !surface->buffer_released)
      owe_release_on(surface, surface->buffer_resource);

    stop_listening_to_buffer(surface);
    surface->buffer_resource = NULL;
    surface->can_draw = false;

    array_remove_element(&tasks_for_draw, surface);

    //TODO this is an unmap, and the window keeps its cell in the layout while
    //it draws nothing. relayouting here means it loses its place to whoever is
    //behind it and takes a different one when it maps again, which is worse
    //until the layout keeps windows in a stable order of its own
    log_debug("Surface detached");
    return;
  }

  //the buffer being replaced is the one the quad has been sampling every frame
  //since it arrived, so it is only now that the client can have it back - and
  //not even now, because this frame may still be on the gpu
  if (surface->buffer_resource && !surface->buffer_released)
    owe_release_on(surface, surface->buffer_resource);

  surface->buffer_resource = buffer_resource;

  //this buffer has not been handed back yet, whatever was true of the last one
  surface->buffer_released = false;

  listen_to_buffer(surface, buffer_resource);

  //a cursor stays out of the draw list however often it is redrawn
  if (surface->is_cursor) {
    wl_buffer_send_release(buffer_resource);
    surface->buffer_released = true;
    return;
  }

  ClientBuffer *buffer = wl_resource_get_user_data(buffer_resource);

  log_debug("Got image with %i %i", buffer->texture.width,
            buffer->texture.heigth);

  surface->client_buffer = buffer;
  surface->image = &buffer->texture;
  memcpy(&surface->model.texture, &buffer->texture, sizeof(PTexture));

  //a dmabuf is already on the gpu. an shm buffer is a mapping of the client's
  //own memory and there is nothing to sample until end_frame() has copied it,
  //so the quad stays out of the picture until then rather than binding the
  //VK_NULL_HANDLE the texture still holds
  if (buffer->type == CLIENT_BUFFER_SHARED_MEMORY) {
    buffer->needs_upload = true;
    surface->can_draw = buffer->texture.image != VK_NULL_HANDLE;
  } else {
    surface->can_draw = true;
  }

  array_add_pointer(&tasks_for_draw, surface);

  surface->x = x;
  surface->y = y;

  //the moment a menu actually has pixels is the moment its placement matters,
  //and it is the one placement nothing else in the compositor can check. the
  //second look is a few frames later, once the shm upload has had a chance to
  //run - at this point can_draw is false for every shm buffer by construction
  if (surface->popup_resource) {
    log_surface_tree("popup attached a buffer");
    surface_tree_dump_countdown = 20;
  }

  log_debug("Surface attached");
}

//the copy, and the release that goes with it. called from end_frame(), the one
//place where nothing is recording a command buffer
void task_upload_shared_memory(Task *surface) {

  ClientBuffer *buffer = surface->client_buffer;

  if (!buffer || buffer->type != CLIENT_BUFFER_SHARED_MEMORY ||
      !buffer->needs_upload)
    return;

  if (!shared_memory_upload(buffer)) {
    surface->can_draw = false;
    return;
  }

  //the image handle only appears on the first upload, so the quad's copy of the
  //texture is stale exactly once
  memcpy(&surface->model.texture, &buffer->texture, sizeof(PTexture));
  surface->can_draw = true;

  //an shm buffer is handed back as soon as it has been copied, and it is the
  //current one rather than the previous one: the compositor is sampling its own
  //image now, not the client's memory, so the client is free to draw into it
  //again. this is the whole difference from the dmabuf path, where the quad
  //goes on reading the client's pages every frame and the release has to wait
  //for a newer buffer to replace this one
  if (surface->buffer_resource && !surface->buffer_released) {
    wl_buffer_send_release(surface->buffer_resource);
    surface->buffer_released = true;
  }
}



void surface_commit(WClient *client, WResource *resource) {

  Task *surface = wl_resource_get_user_data(resource);

  //a client that redraws into a buffer it has already attached commits without
  //attaching again, and an shm buffer is a copy - nothing the client writes
  //reaches the gpu until end_frame() copies it a second time. a dmabuf needs
  //nothing here, because the quad is reading the client's pages directly
  if (surface->client_buffer &&
      surface->client_buffer->type == CLIENT_BUFFER_SHARED_MEMORY)
    surface->client_buffer->needs_upload = true;

  log_debug("Surface committed! Ready to draw.");
}


void handle_frame(WClient *client, WResource *resource, uint32_t callback_id){

  WResource *callback_resource = 
    wl_resource_create(client, &wl_callback_interface, 1, callback_id);

  if (!callback_resource) {
    wl_client_post_no_memory(client);
    log_error("Can't creat frame callback resource");
    return;
  }

  Task *surface = wl_resource_get_user_data(resource);

  //a client asking twice before a frame went out would otherwise leak the
  //first callback and leave it unanswered forever
  if (surface->frame_call_resource)
    send_frame_callback_done(surface);

  surface->frame_call_resource = callback_resource;

}

//the quad is drawn with blending whatever the client says is opaque, so this
//one really does change nothing
static void surface_set_opaque_region(WClient *client, WResource *resource,
                                      WResource *region) {}

//where the client is willing to be pointed at. this is not a hint: a NULL
//region means the whole surface, and an *empty* region means none of it -
//which is how a client says "the pointer belongs to something behind me".
//firefox sets an empty one on the subsurface it renders into, so a compositor
//that ignores this delivers every click to a surface that has already said it
//does not want them, and the mouse does nothing at all
static void surface_set_input_region(WClient *client, WResource *resource,
                                     WResource *region_resource) {

  Task *surface = wl_resource_get_user_data(resource);

  //TODO the protocol double-buffers this: it should be applied on the next
  //commit rather than where it arrives, the same as the subsurface state
  if (!region_resource) {
    region_clean(&surface->input_region);
    surface->has_input_region = false;
    log_debug("Surface takes input over all of itself");
    return;
  }

  Region *region = wl_resource_get_user_data(region_resource);

  region_copy(&surface->input_region, region);
  surface->has_input_region = true;

  log_debug("Surface %p input region set, %i rectangles", (void *)surface,
            surface->input_region.count);

  for (int i = 0; i < surface->input_region.count; i++) {
    RegionOperation *operation = &surface->input_region.operations[i];
    log_debug("  %s %i %i %ix%i", operation->subtract ? "subtract" : "add",
              operation->x, operation->y, operation->width, operation->height);
  }
}

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

  array_remove_element(&tasks_for_draw, surface);

  //the buffer can outlive the surface that attached it, and the listener lives
  //inside the Task that is about to be freed
  stop_listening_to_buffer(surface);
  stop_listening_to_old_buffer(surface);

  //so can the xdg_toplevel, whenever the client destroys the surface first
  task_stop_listening_to_top_level(surface);

  //and so can the wl_subsurface. the tree has to let go of this Task from both
  //directions - the parent still holds it in its children list, and every child
  //still points at it as a parent - before the memory goes away
  forget_subsurface_role(surface);
  forget_popup_role(surface);
  task_detach_subsurfaces(surface);

  //the surface's own copy of whatever region the client last handed it
  region_clean(&surface->input_region);

  //the image is the ClientBuffer's, not the surface's - shm and dma alike.
  //the buffer usually outlives the window it was last drawn into, and its
  //resource destructor retires the image when the client finally lets go of
  //it. cleaning the dma image here destroyed a view a frame in flight was
  //still sampling - the validation error behind every closed window - and
  //destroyed it a second time when the buffer's own teardown reached it

  free(surface);

  //one cell fewer to divide the output into
  layout_apply();

  //forget_task() above dropped the focus if it was on this window, and it is
  //the only one that could: the Task was about to be freed. somebody still has
  //to be given the keyboard afterwards
  layout_focus_fallback();

  log_info("Destroyed surface");
}

void create_surface(WClient *client, WResource *resource,
                    uint32_t id) {

  SwordCompositor *compositor = wl_resource_get_user_data(resource);

  Task *surface = calloc(1, sizeof(Task));




  pe_2d_create_quad_geometry(&surface->model);

  PCreateShaderInfo quad_shader = {
      .transparency = true,
      .out_shader = &surface->model.shader,
      .vertex_path = "/usr/libexec/sword/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/sword/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  pe_vk_create_shader(&quad_shader);


  if (!surface) {
    log_error("Can't create wayland surface");
    wl_client_post_no_memory(client);
    return;
  }

  //the surface inherits the version the client bound wl_compositor at, so a
  //version 4 client gets a surface that accepts the version 4 requests
  surface->resource = wl_resource_create(client, &wl_surface_interface,
                                         wl_resource_get_version(resource), id);
  if (!surface->resource) {
    free(surface);
    log_error("Can't create wayland resource");
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

  //a surface starts out with no parent and no children. parent_link is inited
  //even though nothing is linked through it yet, so wl_list_remove() is safe on
  //a surface that never becomes anyone's child
  wl_list_init(&surface->children);
  wl_list_init(&surface->parent_link);

  surface->compositor = compositor;
  wl_list_insert(&compositor->surfaces, &surface->link);
  log_info("New surface created with ID %u", id);
}
