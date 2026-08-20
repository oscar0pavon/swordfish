#include "input.h"
#include "compositor.h"
#include <complex.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wayland-server-core.h>
#include "../keyboard.h"
#include "data_device.h"
#include "surface.h"
#include <libinput.h>
#include <time.h>
#include "swordfish.h"

//milliseconds before a held key repeats, and repeats per second after that
#define KEYBOARD_REPEAT_DELAY 400
#define KEYBOARD_REPEAT_RATE 25

//keys held right now. wl_keyboard.enter has to carry them, otherwise a client
//that gains focus mid-keystroke never learns the key went down and can wait
//forever for a release it cannot match
#define MAX_PRESSED_KEYS 32
static uint32_t pressed_keys[MAX_PRESSED_KEYS];
static int pressed_keys_count;

//the task holding wl_keyboard focus, which is not the same as focused_task:
//focus is claimed the moment a surface is created, but enter can only be sent
//once that client has actually asked for a keyboard
static Task *keyboard_focus;

//whether that task has actually been sent wl_keyboard.enter yet
static bool focus_entered;

//the task the cursor is inside, and whether it has been sent wl_pointer.enter.
//the same split as the keyboard, for the same reason: the cursor can be over a
//surface before that client has got round to asking for a wl_pointer
static Task *pointer_focus;
static bool pointer_entered;

//where the cursor is, in the render target's own pixels, and where that lands
//inside the surface under it
static double pointer_x, pointer_y;
static double pointer_local_x, pointer_local_y;

// Helper function to get current time in milliseconds
uint32_t get_current_time_msec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

//every event a client can correlate needs its own serial. a constant made all
//of them look like the same event, which is why clients ignored the keys
static uint32_t next_serial(void){
  return wl_display_next_serial(compositor.display);
}

static WResource *focused_keyboard(void){
  if(!keyboard_focus || !keyboard_focus->input)
    return NULL;

  //a client binds wl_seat before it asks for a keyboard, so the resource can
  //still be missing here. wl_keyboard_send_* dereferences it immediately
  return keyboard_focus->input->keyboard_resource;
}

static void track_pressed_key(uint32_t scancode, bool pressed){
  for(int i = 0; i < pressed_keys_count; i++){
    if(pressed_keys[i] != scancode)
      continue;

    if(!pressed)
      pressed_keys[i] = pressed_keys[--pressed_keys_count];

    return;
  }

  if(pressed && pressed_keys_count < MAX_PRESSED_KEYS)
    pressed_keys[pressed_keys_count++] = scancode;
}

static void send_wayland_modifiers(void){

  WResource *keyboard = focused_keyboard();
  if(!keyboard)
    return;

  wl_keyboard_send_modifiers(
      keyboard, next_serial(),
      xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_DEPRESSED),
      xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_LATCHED),
      xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_LOCKED),
      xkb_state_serialize_layout(xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void send_keyboard_enter(void){

  WResource *keyboard = focused_keyboard();
  if(!keyboard)
    return;

  struct wl_array keys;
  wl_array_init(&keys);

  for(int i = 0; i < pressed_keys_count; i++){
    uint32_t *key = wl_array_add(&keys, sizeof(uint32_t));
    if(key)
      *key = pressed_keys[i];
  }

  wl_keyboard_send_enter(keyboard, next_serial(), keyboard_focus->resource,
                         &keys);

  wl_array_release(&keys);

  focus_entered = true;

  //the client's idea of shift and ctrl starts empty, so tell it straight away
  send_wayland_modifiers();

  //the clipboard follows the keyboard: a wl_data_offer belongs to one client,
  //so whatever is on the selection has to be offered again to this one
  data_device_offer_selection(wl_resource_get_client(keyboard_focus->resource));

  printf("Keyboard focus entered\n");
}

//the client holding the keyboard, which is who the selection is offered to.
//NULL until some window has actually been entered
WClient *keyboard_focus_client(void){

  if(!keyboard_focus || !focus_entered)
    return NULL;

  return wl_resource_get_client(keyboard_focus->resource);
}

void set_keyboard_focus(Task *task){

  if(keyboard_focus == task){
    //focus is taken when the surface appears, which is before the client has
    //asked for a keyboard, so the enter it could not be sent then is still owed
    if(!focus_entered)
      send_keyboard_enter();

    return;
  }

  WResource *keyboard = focused_keyboard();
  if(keyboard)
    wl_keyboard_send_leave(keyboard, next_serial(), keyboard_focus->resource);

  keyboard_focus = task;
  focus_entered = false;

  send_keyboard_enter();
}

//the Task is about to be freed and focused_task is a bare global, so anything
//still pointing at it has to let go first or the next key dereferences freed
//memory
void forget_task(Task *task){
  pthread_mutex_lock(&focus_task_mutex);

  if(keyboard_focus == task){
    keyboard_focus = NULL;
    focus_entered = false;
  }

  if(pointer_focus == task){
    pointer_focus = NULL;
    pointer_entered = false;
  }

  if(focused_task == task)
    focused_task = NULL;

  pthread_mutex_unlock(&focus_task_mutex);
}

void forget_task_input(TaskInput *input){
  pthread_mutex_lock(&focus_task_mutex);

  if(keyboard_focus && keyboard_focus->input == input){
    keyboard_focus->input = NULL;
    keyboard_focus = NULL;
    focus_entered = false;
  }

  if(pointer_focus && pointer_focus->input == input){
    pointer_focus->input = NULL;
    pointer_focus = NULL;
    pointer_entered = false;
  }

  if(focused_task && focused_task->input == input)
    focused_task->input = NULL;

  pthread_mutex_unlock(&focus_task_mutex);
}

void send_wayland_key(uint32_t scancode, bool pressed){

  //this is the input thread, and the compositor thread is sending to the same
  //client out of its event loop. lock_wayland() is the outer lock everywhere
  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  track_pressed_key(scancode, pressed);

  WResource *keyboard = focused_keyboard();
  if(!keyboard){
    pthread_mutex_unlock(&focus_task_mutex);
    unlock_wayland();
    return;
  }

  wl_keyboard_send_key(keyboard, next_serial(), get_current_time_msec(),
                       scancode,
                       pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                               : WL_KEYBOARD_KEY_STATE_RELEASED);

  //the key alone does not tell the client that shift went down with it
  send_wayland_modifiers();

  //this runs on the input thread while the compositor thread owns the event
  //loop, so nothing reaches the client until somebody flushes
  wl_display_flush_clients(compositor.display);

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

void send_keyboard_configuration(WResource *resource){
  off_t size;
  int fd = create_keymap_file_descriptor(&size);

  //a client without a keymap is better than a dead compositor
  if(fd < 0){
    printf("No keymap to send to the client\n");
    return;
  }

  wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, 
      fd, size);

  close(fd);

}

//called once a frame from the render loop. focus is settled here rather than
//when the surface is created because the client creates its surface and its
//keyboard as two separate requests, in either order
void handle_focus(){
  //the render thread, and focus_task() is what sends wl_keyboard.enter
  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  focus_task(focused_task);

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

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

  //the render thread walks the same tiles in draw_surface(). lock_wayland()
  //and focus_task_mutex are both held by the caller, and this is the innermost
  //of the three
  pthread_mutex_lock(&draw_tasks_mutex);

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

  pthread_mutex_unlock(&draw_tasks_mutex);

  return hit;
}

//must be called with focus_task_mutex held
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

  //the input thread again, and this one fires as fast as the mouse moves -
  //which is what turned the unsynchronised sends into a disconnected client
  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  pointer_x = x;
  pointer_y = y;

  set_pointer_focus(pointer_hit_task());

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_motion(pointer, get_current_time_msec(),
                           wl_fixed_from_double(pointer_local_x),
                           wl_fixed_from_double(pointer_local_y));

    //this runs on the input thread while the compositor thread owns the event
    //loop, so nothing reaches the client until somebody flushes
    wl_display_flush_clients(compositor.display);
  }

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

void send_wayland_pointer_button(uint32_t button, bool pressed){

  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_button(pointer, next_serial(), get_current_time_msec(),
                           button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                   : WL_POINTER_BUTTON_STATE_RELEASED);

    wl_display_flush_clients(compositor.display);
  }

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

void send_wayland_pointer_axis(double value){

  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  WResource *pointer = focused_pointer();
  if(pointer && pointer_entered){
    wl_pointer_send_axis(pointer, get_current_time_msec(),
                         WL_POINTER_AXIS_VERTICAL_SCROLL,
                         wl_fixed_from_double(value));

    wl_display_flush_clients(compositor.display);
  }

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
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

  pthread_mutex_lock(&focus_task_mutex);

  //focus follows xdg_toplevel, which a cursor surface never gets, so this
  //should not happen. a client that skips xdg-shell entirely still must not be
  //left with the keyboard pointed at its own cursor image
  if(focused_task == cursor)
    focused_task = pointer_focus;

  if(keyboard_focus == cursor){
    keyboard_focus = NULL;
    focus_entered = false;
  }

  pthread_mutex_unlock(&focus_task_mutex);
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

  pthread_mutex_lock(&focus_task_mutex);

  if(input->pointer_resource == resource){
    input->pointer_resource = NULL;
    pointer_entered = false;
  }

  pthread_mutex_unlock(&focus_task_mutex);

  printf("Destroyed pointer\n");
}

static void get_pointer(WClient *client, WResource *resource, uint32_t id) {

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

  pthread_mutex_lock(&focus_task_mutex);

  input->pointer_resource = pointer_resource;

  //the cursor may already be sitting on this client's surface, in which case
  //the enter it was owed could not be sent until now
  if(pointer_focus && pointer_focus->input == input && !pointer_entered)
    send_pointer_enter();

  pthread_mutex_unlock(&focus_task_mutex);
}

static void destroy_task_input(WResource* resource){
  TaskInput* input = wl_resource_get_user_data(resource);
  forget_task_input(input);
  wl_list_remove(&input->link);

  //the keyboard and the pointer are separate resources carrying this same
  //TaskInput, and on a client disconnect libwayland is free to tear them down
  //after the seat. neither destructor may read memory that is about to go
  if(input->keyboard_resource)
    wl_resource_set_user_data(input->keyboard_resource, NULL);

  if(input->pointer_resource)
    wl_resource_set_user_data(input->pointer_resource, NULL);

  free(input);
  printf("Destroyed Task input\n");
}

//wl_keyboard.release, since version 3. without an implementation on the
//resource libwayland dispatches the request through a NULL table
static void keyboard_release(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
  .release = keyboard_release
};

//a client releasing its keyboard leaves the TaskInput holding a resource that
//is already gone, and the next key sends through it
static void destroy_keyboard(WResource *resource) {
  TaskInput *input = wl_resource_get_user_data(resource);

  //the seat resource went first and took the TaskInput with it
  if(!input){
    printf("Destroyed keyboard\n");
    return;
  }

  pthread_mutex_lock(&focus_task_mutex);

  if(input->keyboard_resource == resource){
    input->keyboard_resource = NULL;
    focus_entered = false;
  }

  pthread_mutex_unlock(&focus_task_mutex);

  printf("Destroyed keyboard\n");
}

static void get_keyboard(WClient *client, WResource *resource, uint32_t id) {

  printf("Get keyboard\n");

  TaskInput *input = wl_resource_get_user_data(resource);

  WResource *keyboard_resource;

  uint32_t version = wl_resource_get_version(resource);

  //the keyboard carries the version the client bound the seat at
  keyboard_resource =
      wl_resource_create(client, &wl_keyboard_interface, version, id);
  if (!keyboard_resource) {
    wl_client_post_no_memory(client);
    printf("Can't create keyboard resource\n");
  }

  wl_resource_set_implementation(keyboard_resource, &keyboard_interface,
                                 input, destroy_keyboard);

  input->keyboard_resource = keyboard_resource;

  send_keyboard_configuration(keyboard_resource);

  //since version 4. clients that repeat keys themselves wait for this before
  //they will repeat anything
  if (version >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    wl_keyboard_send_repeat_info(keyboard_resource, KEYBOARD_REPEAT_RATE,
                                 KEYBOARD_REPEAT_DELAY);

  // wl_keyboard_send_modifiers(keyboard_resource,
  //                              234,
  //                              xkb_state_get_mods_depressed(xkb_state),
  //                              xkb_state_get_mods_latched(xkb_state),
  //                              xkb_state_get_mods_locked(xkb_state),
  //                              xkb_state_get_mods_group(xkb_state));

}

static void get_touch(WClient *client, WResource *resource, uint32_t id) {

  printf("Get keyboard\n");
}

static void release(WClient *client, WResource *resource) {

  printf("Release input\n");
}

static const struct wl_seat_interface input_interface = {
  .get_keyboard = get_keyboard, 
  .get_pointer = get_pointer, 
  .get_touch = get_touch, 
  .release = release
};


static void bind_input_handler(WClient *client, void* data, 
    uint32_t version, uint32_t id){


  WResource *resource = wl_resource_create(client, &wl_seat_interface, 
      version, id);

  TaskInput *input = calloc(1, sizeof(TaskInput));
  input->resource = resource;
  input->client = client;
  

  wl_resource_set_implementation(resource, &input_interface, data, NULL);
  wl_resource_set_user_data(resource, input);
  wl_resource_set_destructor(resource, destroy_task_input);

  wl_list_insert(&compositor.tasks_input, &input->link);

  uint32_t capabilities = 0;
  capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
  capabilities |= WL_SEAT_CAPABILITY_POINTER;
  wl_seat_send_capabilities(resource, capabilities);

  //since version 2
  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name(resource, "swordfish");


  printf("Bound input\n");

}

void init_compositor_input(){

  //same story as wl_compositor: pway binds the seat at 4, and version 1 here
  //was a protocol error that killed the client
  wl_global_create(compositor.display, &wl_seat_interface, SEAT_VERSION,
                   &compositor, bind_input_handler);

}


