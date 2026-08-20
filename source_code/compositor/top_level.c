#include "top_level.h"
#include "compositor/compositor.h"
#include <stdint.h>
#include <stdio.h>
#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>
#include "desktop.h"
#include "output.h"
#include "surface.h"
#include <engine/array.h>
#include "swordfish.h"
#include <pthread.h>

typedef struct TopLevel{
  DesktopSurface *surface;
  WResource *resource;
  char *title;
  char *app_id;
  //what the client was last configured at, so a state request can be answered
  //with the size it already has
  int32_t width;
  int32_t height;
  //the client's own limits. recorded because the protocol calls them state,
  //not because anything sizes a window from them yet
  int32_t min_width;
  int32_t min_height;
  int32_t max_width;
  int32_t max_height;
}TopLevel;


void destroy_top_level(WClient *client, WResource *resource){
  printf("Destroy top level\n");
  wl_resource_destroy(resource);
}

//the strings are set again whenever the client feels like it, so the old one
//has to go or every rename leaks
static void replace_string(char **destination, const char *value){
  free(*destination);
  *destination = strdup(value);
}

void set_title(WClient *client, WResource *resource, const char *title){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  replace_string(&top_level->title, title);
  printf("New task with title: %s\n", top_level->title);
}

//the class of application, not the window. clients send this immediately after
//set_title, which is why a NULL here killed the compositor on the first real
//toolkit that connected
static void set_app_id(WClient *client, WResource *resource,
                       const char *app_id){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  replace_string(&top_level->app_id, app_id);
  printf("Task app id: %s\n", top_level->app_id);
}

void send_top_level_configure(TopLevel* toplevel, int width, int height){
  struct wl_array states;
  wl_array_init(&states);

  //no states: not maximized, not fullscreen. that is the honest answer to the
  //requests below, which the protocol says must be answered with a configure
  //whether or not the compositor grants them
  xdg_toplevel_send_configure(toplevel->resource,
      width,
      height,
      &states);

  wl_array_release(&states);

  toplevel->width = width;
  toplevel->height = height;

  //a constant serial made every configure look like the same event, so the
  //client's ack could never be matched to the configure it answered
  uint32_t serial = wl_display_next_serial(compositor.display);
  toplevel->surface->pending_serial = serial;

  xdg_surface_send_configure(toplevel->surface->resource, serial);

}

//swordfish draws every client at the size it already has, so a request to
//change state is answered with a configure carrying the current size and no
//states - "declined". leaving it unanswered is what hangs a client: it waits
//for the configure before it will draw anything again
static void reconfigure(WResource *resource){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  send_top_level_configure(top_level, top_level->width, top_level->height);
}

static void set_maximized(WClient *client, WResource *resource){
  reconfigure(resource);
}

static void unset_maximized(WClient *client, WResource *resource){
  reconfigure(resource);
}

static void set_fullscreen(WClient *client, WResource *resource,
                           WResource *output){
  reconfigure(resource);
}

static void unset_fullscreen(WClient *client, WResource *resource){
  reconfigure(resource);
}

//no configure is owed for this one, and there is nothing to minimize into
static void set_minimized(WClient *client, WResource *resource){}

static void set_max_size(WClient *client, WResource *resource, int32_t width,
                         int32_t height){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  top_level->max_width = width;
  top_level->max_height = height;
}

static void set_min_size(WClient *client, WResource *resource, int32_t width,
                         int32_t height){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  top_level->min_width = width;
  top_level->min_height = height;
}

//dialogs and toolboxes stack above their parent. every task is one quad in the
//scene and nothing stacks, so there is nothing to record
static void set_parent(WClient *client, WResource *resource,
                       WResource *parent){}

//the three requests a client makes on behalf of the pointer. the seat does
//advertise one now, so these do arrive - and there is nothing to do with them
//while every window is a quad drawn at the same place: a move or a resize
//needs a window position and a swapchain that can change size
static void show_window_menu(WClient *client, WResource *resource,
                             WResource *seat, uint32_t serial, int32_t x,
                             int32_t y){}

static void move_top_level(WClient *client, WResource *resource,
                           WResource *seat, uint32_t serial){}

static void resize_top_level(WClient *client, WResource *resource,
                             WResource *seat, uint32_t serial,
                             uint32_t edges){}

const struct xdg_toplevel_interface top_level_implementation = {
  .destroy = destroy_top_level,
  .set_parent = set_parent,
  .set_title = set_title,
  .set_app_id = set_app_id,
  .show_window_menu = show_window_menu,
  .move = move_top_level,
  .resize = resize_top_level,
  .set_max_size = set_max_size,
  .set_min_size = set_min_size,
  .set_maximized = set_maximized,
  .unset_maximized = unset_maximized,
  .set_fullscreen = set_fullscreen,
  .unset_fullscreen = unset_fullscreen,
  .set_minimized = set_minimized
};

static void destroy_top_level_resource(WResource *resource){
  TopLevel *top_level = wl_resource_get_user_data(resource);

  free(top_level->title);
  free(top_level->app_id);
  free(top_level);

  printf("Destroyed top level\n");
}

void get_top_level_implementation(WClient *client,
                                  WResource *resource, uint32_t id) {

  DesktopSurface *surface = wl_resource_get_user_data(resource);

  TopLevel *top_level = calloc(1, sizeof(TopLevel));
  top_level->surface = surface;

  //the toplevel inherits the version its xdg_surface was created at, so a
  //client that bound xdg_wm_base higher gets the events that version added
  top_level->resource = wl_resource_create(
      client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);

  if (!top_level->resource) {
    free(top_level);
    wl_client_post_no_memory(client);
    printf("Can't create top level resource\n");
    return;
  }

  printf("get top level\n");

  wl_resource_set_implementation(top_level->resource, &top_level_implementation,
                                 top_level, destroy_top_level_resource);

  //the newest window takes the keyboard. this is the point at which a surface
  //becomes a window - doing it in create_surface() handed the focus to cursor
  //images and anything else a client makes a bare wl_surface for.
  //handle_focus() on the render thread reads this, so it goes under the same
  //lock as the rest of the focus state
  pthread_mutex_lock(&focus_task_mutex);
  focused_task = surface->surface;
  is_focus_completed = false;
  pthread_mutex_unlock(&focus_task_mutex);

  //the surface is a window now, so it is on the output. sent here rather than
  //in create_surface() for the same reason focus is: a bare wl_surface may be a
  //cursor image, which is on no output. a client binds wl_output in the same
  //registry pass it binds wl_compositor and xdg_wm_base in, so its output
  //resource exists by the time it gets this far
  output_send_surface_enter(surface->surface->resource);

  send_top_level_configure(top_level, 800, 600);

}
