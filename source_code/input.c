#include "input.h"
#include "compositor.h"
#include <complex.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <wayland-server-core.h>
#include "keyboard.h"
#include "data_device.h"
#include "surface.h"
#include <libinput.h>
#include <time.h>
#include "swordfish.h"
#include "pointer.h"
#include "log.h"

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
Task *keyboard_focus;

//whether that task has actually been sent wl_keyboard.enter yet
bool focus_entered;


// Helper function to get current time in milliseconds
uint32_t get_current_time_msec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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

  log_debug("Keyboard focus entered");
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
}

void forget_task_input(TaskInput *input){
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
}

void send_wayland_key(uint32_t scancode, bool pressed){

  track_pressed_key(scancode, pressed);

  WResource *keyboard = focused_keyboard();
  if(!keyboard)
    return;

  wl_keyboard_send_key(keyboard, next_serial(), get_current_time_msec(),
                       scancode,
                       pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                               : WL_KEYBOARD_KEY_STATE_RELEASED);

  //the key alone does not tell the client that shift went down with it
  send_wayland_modifiers();

  //sent right away rather than waiting for the loop's own flush at the top of
  //its next iteration
  wl_display_flush_clients(compositor.display);
}

void send_keyboard_configuration(WResource *resource){
  off_t size;
  int fd = create_keymap_file_descriptor(&size);

  //a client without a keymap is better than a dead compositor
  if(fd < 0){
    log_warn("No keymap to send to the client");
    return;
  }

  wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, 
      fd, size);

  close(fd);

}

//called once a frame from swordfish_frame_step(). focus is settled here rather
//than when the surface is created because the client creates its surface and
//its keyboard as two separate requests, in either order
void handle_focus(){
  focus_task(focused_task);
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
  log_info("Destroyed Task input");
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
    log_info("Destroyed keyboard");
    return;
  }

  if(input->keyboard_resource == resource){
    input->keyboard_resource = NULL;
    focus_entered = false;
  }

  log_info("Destroyed keyboard");
}

static void get_keyboard(WClient *client, WResource *resource, uint32_t id) {

  log_info("Get keyboard");

  TaskInput *input = wl_resource_get_user_data(resource);

  WResource *keyboard_resource;

  uint32_t version = wl_resource_get_version(resource);

  //the keyboard carries the version the client bound the seat at
  keyboard_resource =
      wl_resource_create(client, &wl_keyboard_interface, version, id);
  if (!keyboard_resource) {
    wl_client_post_no_memory(client);
    log_error("Can't create keyboard resource");
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

  log_info("Get keyboard");
}

static void release(WClient *client, WResource *resource) {

  log_info("Release input");
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


  log_info("Bound input");

}

void init_compositor_input(){

  //same story as wl_compositor: pway binds the seat at 4, and version 1 here
  //was a protocol error that killed the client
  wl_global_create(compositor.display, &wl_seat_interface, SEAT_VERSION,
                   &compositor, bind_input_handler);

}


