#include "top_level.h"
#include "compositor/compositor.h"
#include <stdint.h>
#include <stdio.h>
#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>
#include "desktop.h"
#include "engine/array.h"

typedef struct TopLevel{
  DesktopSurface *surface;
  WResource *resource;
  char *title;
}TopLevel;


void destroy_top_level(WClient *client, WResource *resource){
  printf("Destroy top level");
  wl_resource_destroy(resource);
}

void set_title(WClient *client, WResource *resource, const char *title){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  top_level->title = strdup(title);
  printf("new title\n");
  printf("New tack with title: %s\n", top_level->title);

}

const struct xdg_toplevel_interface top_level_implementation = {
  .destroy = destroy_top_level,
  .set_title = set_title
};

void send_top_level_configure(TopLevel* toplevel, int width, int height){
  struct wl_array states;
  wl_array_init(&states);

  xdg_toplevel_send_configure(toplevel->resource,
      width,
      height,
      &states);

  wl_array_release(&states);
  uint32_t serial = 30;
  xdg_surface_send_configure(toplevel->surface->resource, serial);

}

void get_top_level_implementation(WClient *client,
                                  WResource *resource, uint32_t id) {

  DesktopSurface *surface = wl_resource_get_user_data(resource);

  TopLevel *top_level = calloc(1, sizeof(TopLevel));
  top_level->surface = surface;

  top_level->resource =
      wl_resource_create(client, &xdg_toplevel_interface, 1, id);

  printf("get top level\n");

  wl_resource_set_implementation(top_level->resource, &top_level_implementation, top_level,
                                 NULL);

  send_top_level_configure(top_level, 800, 600);

}
