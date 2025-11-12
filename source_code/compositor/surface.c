#include "surface.h"

#include "compositor.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

static void surface_destroy(WaylandClient *client, WaylandResource *resource) {
  // The client asked to destroy the surface resource. The general resource
  // destroy function (below) will be called after this.
  printf("Surface destroy\n");
}

static void surface_attach(WaylandClient *client, WaylandResource *resource,
                           WaylandResource *buffer_resource, int32_t x,
                           int32_t y) {
  SwordfishSurface *surface = wl_resource_get_user_data(resource);
  // Store the pending buffer and offsets. This is not the "live" buffer yet.
  surface->buffer = wl_resource_get_user_data(buffer_resource);
  surface->x = x;
  surface->y = y;
  // Note: If buffer_resource is NULL, the client wants to detach the buffer.
  printf("Surface attach\n");
}

static void surface_damage(WaylandClient *client, WaylandResource *resource,
                           int32_t x, int32_t y, int32_t width,
                           int32_t height) {
  // Store the damaged region information.
  // ...
  printf("Surface damage\n");
}

static void surface_commit(WaylandClient *client, WaylandResource *resource) {
  SwordfishSurface *surface = wl_resource_get_user_data(resource);

  // This is the CRUCIAL step. The client is telling the compositor to
  // apply all pending state changes (the attach, damage, etc.) atomically.

  // In a real compositor, you transition pending state to current state:
  // surface->current_buffer = surface->pending_buffer;
  // surface->current_damage = surface->pending_damage;

  // After commit, you signal your rendering loop that this surface
  // is ready to be drawn and needs a redraw operation soon.
  printf("Surface committed! Ready to draw.\n");
  // e.g., schedule_repaint(surface->comp);
}

// Define the full interface implementation structure:
const struct wl_surface_interface surface_implementation = {
    .destroy = surface_destroy,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = NULL, // Implement wl_surface.frame for VSync callbacks
    .set_opaque_region = NULL,
    .set_input_region = NULL,
    .commit = surface_commit,
    // ... other functions
};

static void destroy_surface(WaylandResource *resource) {
  SwordfishSurface *surface = wl_resource_get_user_data(resource);
  wl_list_remove(&surface->link);

  free(surface);
  printf("Destroyed surface\n");
}

void create_surface(WaylandClient *client, WaylandResource *resource,
                    uint32_t id) {

  printf("Creating surface\n");

  SwordfishCompositor *compositor = wl_resource_get_user_data(resource);
  SwordfishSurface *surface = calloc(1, sizeof(SwordfishSurface));
  printf("allocated surface\n");

  if (!surface) {
    printf("Can't create wayland surface\n");
    wl_client_post_no_memory(client);
    return;
  }

  // 1. Create the new server-side Wayland resource
  surface->resource = wl_resource_create(client, &wl_surface_interface, 1, id);
  if (!surface->resource) {
    free(surface);
    printf("Can't create wayland resource\n");
    wl_client_post_no_memory(client);
    return;
  }
  printf("Created resource\n");

  // 2. Link our C struct to the Wayland resource handle
  wl_resource_set_implementation(surface->resource, &surface_implementation,
                                 surface,
                                 destroy_surface); // Set the destroy handler

  printf("implementation\n");

  // 3. Add to the compositor's list of surfaces
  surface->compositor = compositor;
  wl_list_insert(&compositor->surfaces, &surface->link);
  printf("New surface created with ID %u\n", id);
}
