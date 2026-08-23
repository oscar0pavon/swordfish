#include "desktop.h"
#include "compositor.h"
#include "desktop-server.h"
#include <complex.h>
#include <stdio.h>
#include <stdlib.h>

#include "popup.h"
#include "top_level.h"
#include "log.h"

//recieve from client
void do_desktop_ack(WClient* client, WResource* resource, uint32_t serial){

  DesktopSurface * desktop_surface = wl_resource_get_user_data(resource);

  //the configure the client is answering. an ack for anything else is a stale
  //reply to a configure that has already been superseded, and the state that
  //came with it must not be applied
  if(serial != desktop_surface->pending_serial){
    log_warn("Client acked configure %u, waiting for %u", serial,
             desktop_surface->pending_serial);
    return;
  }

  desktop_surface->pending_serial = 0;

  log_debug("ack %u", serial);
}

void do_desktop_pong(WClient* client, WResource* resource, uint32_t serial){
  //TODO respond to client
  log_debug("pong");
}

//every request in the interface needs a handler: libwayland dispatches a NULL
//entry as a call, so a client sending one of these takes the compositor down
static void desktop_surface_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

//the part of the surface that is the window proper, the rest being the client's
//own shadows and borders. recorded rather than used: the quad still samples the
//whole buffer
static void desktop_surface_set_window_geometry(WClient *client,
                                                WResource *resource, int32_t x,
                                                int32_t y, int32_t width,
                                                int32_t height) {

  DesktopSurface *desktop_surface = wl_resource_get_user_data(resource);

  desktop_surface->geometry_x = x;
  desktop_surface->geometry_y = y;
  desktop_surface->geometry_width = width;
  desktop_surface->geometry_height = height;
}

const struct xdg_surface_interface desktop_surface_implementation = {
    .destroy = desktop_surface_destroy,
    .get_toplevel = get_top_level_implementation,
    .get_popup = create_popup,
    .set_window_geometry = desktop_surface_set_window_geometry,
    .ack_configure = do_desktop_ack,
};

static void destroy_desktop_surface(WResource *resource) {
  DesktopSurface *desktop_surface = wl_resource_get_user_data(resource);

  free(desktop_surface);

  log_info("Destroyed desktop surface");
}

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
                                 desktop_surface, destroy_desktop_surface);

  log_info("Get desktop surface");
}

static void desktop_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

const struct xdg_wm_base_interface desktop_implementation = {
  .destroy = desktop_destroy,
  .create_positioner = create_positioner,
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
  log_info("Desktop bound");
}
