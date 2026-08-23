#include "top_level.h"
#include "compositor.h"
#include <stdint.h>
#include <stdio.h>
#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>
#include "desktop.h"
#include "layout.h"
#include "output.h"
#include "surface.h"
#include <engine/array.h>
#include "swordfish.h"
#include <pthread.h>
#include "input.h"
#include "mouse.h"
#include "outputs.h"

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

//the protocol has no way to make a client go away - xdg_toplevel.close is a
//request, and a client with unsaved work is entitled to answer it with a
//dialog instead
void top_level_close(TopLevel *top_level){
  xdg_toplevel_send_close(top_level->resource);
}

//a tiled window is already the only size it is going to get, so a request to
//change state is answered with a configure carrying the size the layout gave
//it and no states - "declined". leaving it unanswered is what hangs a client:
//it waits for the configure before it will draw anything again
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
//advertise one now, so these do arrive - and a tiled window's position and
//size are not the client's to ask for. dragging one somewhere else would mean
//a layout that remembers a window was pulled out of the tiling, which is a
//floating layer and does not exist
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

//the window is gone but its wl_surface may not be - a client is free to drop
//the toplevel role and keep the surface. the task stops counting as a window
//from here
static void handle_top_level_destroyed(struct wl_listener *listener,
                                       void *data){

  Task *task = wl_container_of(listener, task, top_level_destroy);

  task->top_level = NULL;
  task->listening_to_top_level = false;

  //re-inited rather than just removed, so task_stop_listening_to_top_level()
  //can remove it again without walking a stale link
  wl_list_remove(&task->top_level_destroy.link);
  wl_list_init(&task->top_level_destroy.link);
}

void task_stop_listening_to_top_level(Task *task){

  if(!task->listening_to_top_level)
    return;

  wl_list_remove(&task->top_level_destroy.link);
  task->listening_to_top_level = false;
}

static void destroy_top_level_resource(WResource *resource){
  TopLevel *top_level = wl_resource_get_user_data(resource);

  //nothing is reached back through here. the task's pointer to this toplevel
  //is cleared by handle_top_level_destroyed(), which libwayland has already
  //called - going the other way round dereferenced a wl_surface that a
  //disconnecting client had taken with it, and killed the compositor every
  //time a window closed

  free(top_level->title);
  free(top_level->app_id);
  free(top_level);

  //one window fewer, so the survivors grow into what it had
  layout_apply();

  //a client that destroys its xdg_toplevel and keeps the wl_surface leaves the
  //Task alive but no longer a window, so the focus has to move even though
  //nothing was freed
  layout_focus_fallback();

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

  //this is the point where a wl_surface becomes a window, and so the point
  //where it starts counting for the layout. the listener is what makes the
  //pointer safe to keep: either side may be destroyed first
  surface->surface->top_level = top_level;
  surface->surface->top_level_destroy.notify = handle_top_level_destroyed;
  wl_resource_add_destroy_listener(top_level->resource,
                                   &surface->surface->top_level_destroy);
  surface->surface->listening_to_top_level = true;

  //the surface is a window now, so it is on the output. sent here rather than
  //in create_surface() for the same reason focus is: a bare wl_surface may be a
  //cursor image, which is on no output. a client binds wl_output in the same
  //registry pass it binds wl_compositor and xdg_wm_base in, so its output
  //resource exists by the time it gets this far
  //
  //which output is wherever the cursor happened to be when the window was
  //created - there is no other signal to place it by, and it stays put after
  //this: nothing currently moves a mapped window to a different output
  surface->surface->output_index = swordfish_output_index_at(cursor_x);
  output_send_surface_enter(surface->surface->resource,
                            surface->surface->output_index);

  //the initial configure comes out of the layout like every other one: the new
  //window is given a cell and everything already on screen is told to make
  //room for it. this is the only configure a client gets before it may draw,
  //so the layout has to run here and not on the first commit
  layout_apply();

}
