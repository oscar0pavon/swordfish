
#include "pointer.h"
#include "surface.h"
#include "input.h"

//the task the cursor is inside, and whether it has been sent wl_pointer.enter.
//the same split as the keyboard, for the same reason: the cursor can be over a
//surface before that client has got round to asking for a wl_pointer
Task *pointer_focus;
bool pointer_entered;

//where the cursor is, in the render target's own pixels, and where that lands
//inside the surface under it
static double pointer_x, pointer_y;
static double pointer_local_x, pointer_local_y;


static WResource *focused_pointer(void){
  if(!pointer_focus || !pointer_focus->input)
    return NULL;

  //a client binds wl_seat before it asks for a pointer, so the resource can
  //still be missing here
  return pointer_focus->input->pointer_resource;
}

static void send_pointer_enter(void){

  WResource *pointer = focused_pointer();
  if(!pointer)
    return;

  wl_pointer_send_enter(pointer, next_serial(), pointer_focus->resource,
                        wl_fixed_from_double(pointer_local_x),
                        wl_fixed_from_double(pointer_local_y));

  pointer_entered = true;

  printf("Pointer entered task at %.0f %.0f\n", pointer_local_x,
         pointer_local_y);
}

//is the cursor inside this window's cell, and where in the client's own buffer
//does that land. the quad is stretched into the cell it was given, so the
//buffer is not the same size as the rectangle on screen and the position has
//to be scaled back - a client told the pointer is at the tile's coordinates
//puts its cursor somewhere other than where the user is pointing
static bool pointer_inside(Task *task){

  int32_t x = task->tile_x;
  int32_t y = task->tile_y;
  int32_t width = task->tile_width;
  int32_t height = task->tile_height;

  //a surface the layout never reached is drawn at the corner at its own size
  if(width == 0){
    x = 0;
    y = 0;
    width = task->image->width;
    height = task->image->heigth;
  }

  if(pointer_x < x || pointer_y < y)
    return false;

  if(pointer_x >= x + width || pointer_y >= y + height)
    return false;

  pointer_local_x = (pointer_x - x) * (double)task->image->width / width;
  pointer_local_y = (pointer_y - y) * (double)task->image->heigth / height;

  return true;
}

//which surface the cursor is over. every window has its own cell now, so this
//walks them and takes the first one the cursor is inside - front to back,
//which for a tiling layout is any order at all since the cells do not overlap.
//once the quads move into the 3d world this becomes a ray cast, and it is the
//only thing that has to change
static Task *pointer_hit_task(void){

  if(pointer_x < 0 || pointer_y < 0)
    return NULL;

  Task *task;

  Task *hit = NULL;

  wl_list_for_each(task, &compositor.surfaces, link){

    //no buffer means no size to test against, and only a surface the client
    //made a toplevel out of has a cell to be inside - a cursor image or a
    //surface still on its way to being a window would otherwise take the
    //pointer over the whole rectangle it would be drawn at
    if(!task->can_draw || !task->top_level || task->is_cursor || !task->image)
      continue;

    if(pointer_inside(task)){
      hit = task;
      break;
    }
  }

  return hit;
}

static void set_pointer_focus(Task *task){

  if(pointer_focus == task){
    //the cursor was already inside when the client finally asked for a
    //pointer, so the enter that could not be sent then is still owed
    if(task && !pointer_entered)
      send_pointer_enter();

    return;
  }

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered)
    wl_pointer_send_leave(pointer, next_serial(), pointer_focus->resource);

  pointer_focus = task;
  pointer_entered = false;

  send_pointer_enter();
}

void send_wayland_pointer_motion(double x, double y){

  pointer_x = x;
  pointer_y = y;

  set_pointer_focus(pointer_hit_task());

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_motion(pointer, get_current_time_msec(),
                           wl_fixed_from_double(pointer_local_x),
                           wl_fixed_from_double(pointer_local_y));

    //sent right away rather than waiting for the loop's own flush at the top
    //of its next iteration
    wl_display_flush_clients(compositor.display);
  }
}

void send_wayland_pointer_button(uint32_t button, bool pressed){

  //click to focus. the pointer already goes to whatever cell the cursor is in,
  //but the keyboard follows focused_task, which until now only super+j/k and a
  //new window ever moved - so clicking a terminal left the keys going to the
  //one that happened to be focused. only on the press: a release belongs to
  //whoever took the press, and pointer_hit_task() has already ruled out
  //anything that is not a window
  if(pressed && pointer_focus && pointer_focus != focused_task){
    focused_task = pointer_focus;

    //swordfish_frame_step()'s handle_focus() is what turns this into
    //wl_keyboard.leave, enter and a fresh clipboard offer
    is_focus_completed = false;
    printf("Focus moved to the clicked window\n");
  }

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_button(pointer, next_serial(), get_current_time_msec(),
                           button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                   : WL_POINTER_BUTTON_STATE_RELEASED);

    wl_display_flush_clients(compositor.display);
  }
}

void send_wayland_pointer_axis(double value){

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_axis(pointer, get_current_time_msec(),
                         WL_POINTER_AXIS_VERTICAL_SCROLL,
                         wl_fixed_from_double(value));

    wl_display_flush_clients(compositor.display);
  }
}

//the client's own cursor image. the surface came from wl_compositor.create_
//surface like any other, so without this it is drawn as a full quad in the
//scene - and, being a surface, it would take the focus that goes with one
static void pointer_set_cursor(WClient *client, WResource *resource,
                               uint32_t serial, WResource *surface_resource,
                               int32_t hotspot_x, int32_t hotspot_y) {

  //a NULL surface means hide the cursor, and nothing is drawn to hide
  if(!surface_resource)
    return;

  Task *cursor = wl_resource_get_user_data(surface_resource);
  if(!cursor)
    return;

  mark_surface_as_cursor(cursor);

  //focus follows xdg_toplevel, which a cursor surface never gets, so this
  //should not happen. a client that skips xdg-shell entirely still must not be
  //left with the keyboard pointed at its own cursor image
  if(focused_task == cursor)
    focused_task = pointer_focus;

  if(keyboard_focus == cursor){
    keyboard_focus = NULL;
    focus_entered = false;
  }
}

//wl_pointer.release, since version 3. without an implementation on the
//resource libwayland dispatches the request through a NULL table
static void pointer_release(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_interface = {
  .set_cursor = pointer_set_cursor,
  .release = pointer_release
};

//the resource is dropped from the TaskInput here rather than in
//forget_task_input, which is only reached when the whole seat goes away: a
//client releasing just its pointer would otherwise leave a freed resource
//behind for the next motion event to send through
static void destroy_pointer(WResource *resource) {
  TaskInput *input = wl_resource_get_user_data(resource);

  //the seat resource went first and took the TaskInput with it
  if(!input){
    printf("Destroyed pointer\n");
    return;
  }

  if(input->pointer_resource == resource){
    input->pointer_resource = NULL;
    pointer_entered = false;
  }

  printf("Destroyed pointer\n");
}

void get_pointer(WClient *client, WResource *resource, uint32_t id) {

  printf("Get pointer\n");

  TaskInput *input = wl_resource_get_user_data(resource);

  //the pointer carries the version the client bound the seat at
  uint32_t version = wl_resource_get_version(resource);

  WResource *pointer_resource =
      wl_resource_create(client, &wl_pointer_interface, version, id);
  if (!pointer_resource) {
    wl_client_post_no_memory(client);
    printf("Can't create pointer resource\n");
    return;
  }

  wl_resource_set_implementation(pointer_resource, &pointer_interface, input,
                                 destroy_pointer);

  input->pointer_resource = pointer_resource;

  //the cursor may already be sitting on this client's surface, in which case
  //the enter it was owed could not be sent until now
  if(pointer_focus && pointer_focus->input == input && !pointer_entered)
    send_pointer_enter();
}
