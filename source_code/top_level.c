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
#include "sword.h"
#include "input.h"
#include "mouse.h"
#include "outputs.h"
#include "log.h"

void destroy_top_level(WClient *client, WResource *resource){
  log_info("Destroy top level");
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
  log_info("New task with title: %s", top_level->title);
}

//the class of application, not the window. clients send this immediately after
//set_title, which is why a NULL here killed the compositor on the first real
//toolkit that connected
static void set_app_id(WClient *client, WResource *resource,
                       const char *app_id){
  TopLevel *top_level = wl_resource_get_user_data(resource);
  replace_string(&top_level->app_id, app_id);
  log_info("Task app id: %s", top_level->app_id);
}

static void add_state(struct wl_array *states, uint32_t state){
  uint32_t *entry = wl_array_add(states, sizeof(*entry));
  *entry = state;
}

//the tiled states are since version 2, so a client that bound xdg_wm_base at 1
//still gets the empty array. a floating window is not tiled in any sense the
//protocol means: it really is free-standing, and the shadow under it is what
//makes it read as sitting over the tiling rather than in it
static bool top_level_is_tiled(TopLevel *toplevel){

  if(wl_resource_get_version(toplevel->resource) <
     XDG_TOPLEVEL_STATE_TILED_LEFT_SINCE_VERSION)
    return false;

  //reconfigure() reaches this straight from a client request, so neither half
  //of the chain back to the Task can be assumed to be there
  if(!toplevel->surface || !toplevel->surface->surface)
    return false;

  return !toplevel->surface->surface->is_floating;
}

void send_top_level_configure(TopLevel* toplevel, int width, int height){
  struct wl_array states;
  wl_array_init(&states);

  //not maximized and not fullscreen, which is the honest answer to the
  //requests below - the protocol says they must be answered with a configure
  //whether or not the compositor grants them.
  //
  //the four tiled states are here because a configure carrying *no* states is
  //a suggestion: the protocol says the size in it is a hint and the client may
  //take its own instead. firefox does - it maps at the cell it was given, then
  //restores its session and redraws at the size it remembered from the last
  //run. the layout only sends a configure when the cell changes, so nothing
  //ever took that back and the window sat squashed into a cell it was nearly
  //twice the height of until super+f floated it and forced a fresh one.
  //
  //they also tell a client with its own decorations that every edge of it is
  //against something, so it drops the shadow it draws *outside* its window
  //geometry - the 26px firefox pads all four sides with, which the quad has no
  //way to know is not part of the window and stretched into the cell along
  //with it. the buffer comes out the size of the cell exactly
  if(top_level_is_tiled(toplevel)){
    add_state(&states, XDG_TOPLEVEL_STATE_TILED_LEFT);
    add_state(&states, XDG_TOPLEVEL_STATE_TILED_RIGHT);
    add_state(&states, XDG_TOPLEVEL_STATE_TILED_TOP);
    add_state(&states, XDG_TOPLEVEL_STATE_TILED_BOTTOM);
  }

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

  //the one record of what the compositor actually asked a client for. every
  //"the window is stretched" report is a question about whether this went out
  //and whether the client answered it, and without the line there is nothing
  //in the log but the buffer sizes to infer it from
  log_info("Configure %ix%i serial %u to \"%s\"", width, height, serial,
           toplevel->title ? toplevel->title : "(no title)");
}

//the protocol has no way to make a client go away - xdg_toplevel.close is a
//request, and a client with unsaved work is entitled to answer it with a
//dialog instead
void top_level_close(TopLevel *top_level){
  xdg_toplevel_send_close(top_level->resource);
}

//a tiled window is already the only size it is going to get, so a request to
//change state is answered with a configure carrying the size the layout gave
//it and neither maximized nor fullscreen among its states - "declined".
//leaving it unanswered is what hangs a client: it waits for the configure
//before it will draw anything again
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

//the three requests a client makes on behalf of the pointer. a tiled window's
//position and size are still not the client's to ask for. there is a floating
//layer now (layout_toggle_floating(), pointer.c's super+drag) but these three
//stay declined even for a floating window: super+drag already covers move and
//resize from the compositor's own side, and wiring a CSD client's own
//titlebar drag through wl_seat's grab/serial machinery is its own chunk of
//work that nothing here has needed yet
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

  log_info("Destroyed top level");
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
    log_error("Can't create top level resource");
    return;
  }

  log_info("get top level");

  wl_resource_set_implementation(top_level->resource, &top_level_implementation,
                                 top_level, destroy_top_level_resource);

  //the newest window takes the keyboard. this is the point at which a surface
  //becomes a window - doing it in create_surface() handed the focus to cursor
  //images and anything else a client makes a bare wl_surface for
  focused_task = surface->surface;
  is_focus_completed = false;

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
  //created - there is no other signal to place it by. a tiled window stays
  //put after this; a floating one's output_index can still move, but only
  //by being dragged there - see apply_drag() in pointer.c
  surface->surface->output_index = sword_output_index_at(cursor_x);
  output_send_surface_enter(surface->surface->resource,
                            surface->surface->output_index);

  log_debug("Window mapped on output %i (cursor at %.0f)",
            surface->surface->output_index, cursor_x);

  //the initial configure comes out of the layout like every other one: the new
  //window is given a cell and everything already on screen is told to make
  //room for it. this is the only configure a client gets before it may draw,
  //so the layout has to run here and not on the first commit
  layout_apply();

}
