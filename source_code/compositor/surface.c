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
#include "engine/array.h"
#include "engine/images.h"
#include "engine/engine2d.h"
#include "renderer/pipeline.h"
#include "renderer/descriptor_set.h"

Array surface_to_draw;

static void surface_damage(WaylandClient *client, WaylandResource *resource,
                           int32_t x, int32_t y, int32_t width,
                           int32_t height) {
  // Store the damaged region information.
  // ...
  printf("Surface damage\n");
}

static void surface_destroy(WaylandClient *client, WaylandResource *resource) {
  // The client asked to destroy the surface resource. The general resource
  // destroy function (below) will be called after this.
  printf("Surface destroy\n");
}

void send_frame_callback_done(SwordfishSurface *surface){
  wl_callback_send_done(surface->frame_call_resource, 1);
  wl_resource_destroy(surface->frame_call_resource);
  surface->frame_call_resource = NULL;
}

void surface_attach(WaylandClient *client, WaylandResource *resource,
                           WaylandResource *buffer_resource, int32_t x,
                           int32_t y) {

  SwordfishSurface *surface = wl_resource_get_user_data(resource);

  PTexture *image_buffer = wl_resource_get_user_data(buffer_resource);

  printf("Go image with %i %i\n", image_buffer->width, image_buffer->heigth);
  surface->image = image_buffer;
  memcpy(&surface->model.texture, image_buffer, sizeof(PTexture));

  surface->x = x;
  surface->y = y;

  printf("Surface attach\n");
}



void surface_commit(WaylandClient *client, WaylandResource *resource) {

  SwordfishSurface *surface = wl_resource_get_user_data(resource);


  array_add_pointer(&surface_to_draw, surface);


  printf("Surface committed! Ready to draw.\n");
}


void handle_frame(WaylandClient *client, WaylandResource *resource, uint32_t callback_id){

  printf("Client requested frame callback with ID %u\n", callback_id);

  WaylandResource *callback_resource = 
    wl_resource_create(client, &wl_callback_interface, 1, callback_id);

  if (!callback_resource) {
    wl_client_post_no_memory(client);
    printf("Can't creat frame callback resource\n");
    return;
  }

  SwordfishSurface *surface = wl_resource_get_user_data(resource);
  surface->frame_call_resource = callback_resource;

}

const struct wl_surface_interface surface_implementation = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = handle_frame, 
    .set_opaque_region = NULL,
    .set_input_region = NULL,
    .commit = surface_commit,
};

static void destroy_surface(WaylandResource *resource) {
  SwordfishSurface *surface = wl_resource_get_user_data(resource);
  wl_list_remove(&surface->link);

  free(surface);
  printf("Destroyed surface\n");
}

void create_surface(WaylandClient *client, WaylandResource *resource,
                    uint32_t id) {

  SwordfishCompositor *compositor = wl_resource_get_user_data(resource);
  SwordfishSurface *surface = calloc(1, sizeof(SwordfishSurface));


  pe_2d_create_quad_geometry(&surface->model);

  PCreateShaderInfo quad_shader = {
      .transparency = true,
      .out_pipeline = &surface->model.pipeline,
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

  surface->resource = wl_resource_create(client, &wl_surface_interface, 1, id);
  if (!surface->resource) {
    free(surface);
    printf("Can't create wayland resource\n");
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(surface->resource, &surface_implementation,
                                 surface,
                                 destroy_surface); // Set the destroy handler

  surface->compositor = compositor;
  wl_list_insert(&compositor->surfaces, &surface->link);
  printf("New surface created with ID %u\n", id);
}
