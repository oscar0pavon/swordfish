#include "input.h"
#include "compositor.h"
#include <stdint.h>
#include <stdio.h>
#include <wayland-server-protocol.h>
#include <wayland-server-core.h>

static void get_pointer(WClient *client, WResource *resource, uint32_t id) {

  printf("Get pointer\n");
}

static void get_keyboard(WClient *client, WResource *resource, uint32_t id) {

  printf("Get keyboard\n");

  WResource *keyboard_resource;

  keyboard_resource =
      wl_resource_create(client, &wl_compositor_interface, 1, id);
  if (!keyboard_resource) {
    wl_client_post_no_memory(client);
    printf("Can't create keyboard resource\n");
  }
 



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

  wl_resource_set_implementation(resource, &input_interface, data, NULL);

  uint32_t key_board_cap = 0;
  key_board_cap |= WL_SEAT_CAPABILITY_KEYBOARD;
  wl_seat_send_capabilities(resource, key_board_cap);

  printf("Bound input\n");

}

void init_compositor_input(){

  wl_global_create(compositor.display, &wl_seat_interface, 1, &compositor,
                   bind_input_handler);

}


