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
#include "surface.h"
#include <libinput.h>
#include <time.h>
#include "swordfish.h"

//milliseconds before a held key repeats, and repeats per second after that
#define KEYBOARD_REPEAT_DELAY 400
#define KEYBOARD_REPEAT_RATE 25

// Helper function to get current time in milliseconds
uint32_t get_current_time_msec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


void send_wayland_key(uint32_t scancode, uint32_t event_state){

  if(!focused_task){
    printf("not focused task\n");
    return;
  }

  if(!focused_task->input){
    printf("not input task\n");
    return;
  }

  WResource *keyboard = focused_task->input->keyboard_resource;

  //the client binds wl_seat before it asks for a keyboard, so focus_task() can
  //hand us an input with no keyboard_resource yet. wl_keyboard_send_key
  //dereferences the resource immediately, so a keypress landing in that window
  //took the whole compositor down - only ever on the libinput path, since the
  //pway path never calls this
  if(!keyboard){
    printf("focused task has no keyboard yet\n");
    return;
  }

  uint32_t timestamp = get_current_time_msec();

   uint32_t wl_state = (event_state == LIBINPUT_KEY_STATE_PRESSED) ? 
                        WL_KEYBOARD_KEY_STATE_PRESSED : 
                        WL_KEYBOARD_KEY_STATE_RELEASED;
  
   printf("Sent key\n");
  wl_keyboard_send_key(keyboard,
                         234,
                         timestamp,
                         scancode,
                         wl_state);
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

void handle_focus(){
 // pthread_mutex_lock(&focus_task_mutex); 
 // printf("testing focus\n");
 //  if(!is_focus_completed){
 //
 //    if(!focused_task)
 //      return;
 //    if(focused_task->input == NULL)
 //      return;
 //    if(focused_task->input->keyboard_resource == NULL)
 //      return;
 //
 //    focus_task(focused_task);
 //    is_focus_completed = true;
 //    focused_task = NULL;
 //  }
 // pthread_mutex_unlock(&focus_task_mutex); 
 // printf("end testing focus\n");

  focus_task(focused_task);
}

static void get_pointer(WClient *client, WResource *resource, uint32_t id) {

  printf("Get pointer\n");
}

static void destroy_task_input(WResource* resource){
  TaskInput* input = wl_resource_get_user_data(resource);
  wl_list_remove(&input->link);
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
                                 input, NULL);

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

  uint32_t key_board_cap = 0;
  key_board_cap |= WL_SEAT_CAPABILITY_KEYBOARD;
  wl_seat_send_capabilities(resource, key_board_cap);

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


