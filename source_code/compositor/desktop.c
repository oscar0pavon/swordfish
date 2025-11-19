#include "desktop.h"
#include "compositor.h"
#include "compositor/desktop-server.h"
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>

#include "top_level.h"

//recieve from client
void do_desktop_ack(WClient* client, WResource* resource, uint32_t serial){

  DesktopSurface * desktop_surface = wl_resource_get_user_data(resource);
  Task *surface = desktop_surface->surface;

  
   
  
  printf("ack\n");
}

void do_desktop_pong(WClient* client, WResource* resource, uint32_t serial){
  //TODO respond to client
  printf("pong\n");
}

const struct xdg_surface_interface desktop_surface_implementation = {
    .destroy = NULL,
    .get_toplevel = get_top_level_implementation,
    .get_popup = NULL,
    .set_window_geometry = NULL,
    .ack_configure = do_desktop_ack,
};

void get_desktop_surface(WClient *client, WResource *resource,
    uint32_t id, WResource *surface_resource) {

  SwordfishCompositor* compositor = wl_resource_get_user_data(resource);
  Task* surface = wl_resource_get_user_data(surface_resource);

  DesktopSurface *desktop_surface = calloc(1, sizeof(DesktopSurface));

  desktop_surface->surface = surface;
  desktop_surface->resource = wl_resource_create(
      client, &xdg_surface_interface, wl_resource_get_version(resource), id);

  wl_resource_set_implementation(desktop_surface->resource,
                                 &desktop_surface_implementation,
                                 desktop_surface, NULL);

  printf("Get desktop surface\n");
}

const struct xdg_wm_base_interface desktop_implementation = {
  .destroy = NULL,
  .get_xdg_surface = get_desktop_surface,
  .pong = do_desktop_pong
};



void bind_desktop(WClient *client, void *data, uint32_t version,
                       uint32_t id) {
  
  SwordfishCompositor* compositor = (SwordfishCompositor*)data;
  WResource *resource;

  resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &desktop_implementation, data, NULL);
  printf("Desktop bound\n");
}
