#define _GNU_SOURCE
#include <stdint.h>
#include "keyboard.h"
#include "build.h"
#include "compositor/compositor.h"
#include "input.h"
#include "swordfish.h"
#include <complex.h>
#include <stdio.h>
#include <libinput.h>
#include <libseat.h>
#include <fcntl.h>
#include <mman.h>
#include <string.h>
#include <unistd.h>

struct xkb_context *xkb_context;
struct xkb_keymap *xkb_keymap;
struct xkb_state *xkb_state;

//return file descriptor of keymap
int create_keymap_file_descriptor(off_t *size_out){
  char *keymap_string = xkb_keymap_get_as_string(xkb_keymap, 
      XKB_KEYMAP_FORMAT_TEXT_V1);
  if(!keymap_string){
    printf("Failed to get XKB keymap string\n");
    return -1;
  }

  size_t size = strlen(keymap_string);

  int fd = memfd_create("swordfish-keyboard", MFD_CLOEXEC);
  if(fd < 0){
    printf("Can't create file descriptor for keyboard\n");
    free(keymap_string);
    return -1;
  }

  if(ftruncate(fd, size) < 0){
   printf("Can't truncate keyboard file descriptor\n");
   close(fd);
   free(keymap_string);
  }

  write(fd, keymap_string, size);

  free(keymap_string);

  *size_out = size;

  return fd;

}

off_t get_keymap_file_size(int fd){
  off_t size = lseek(fd, 0, SEEK_END);
  lseek(fd,0, SEEK_SET);
  return size;
}




void handle_xkb_keyboard_event(InputEvent *event) {
  InputEventKeyboard *key_event = libinput_event_get_keyboard_event(event);


  uint32_t scancode = libinput_event_keyboard_get_seat_key_count(key_event);

  enum libinput_key_state key_state =
      libinput_event_keyboard_get_key_state(key_event);

  uint32_t key_code = libinput_event_keyboard_get_key(key_event);
 
  //send keys to clients
  printf("keyboard event\n");
  send_wayland_key(scancode, key_state);

  enum xkb_key_direction direction;
  
  // XKB uses keycodes offset by 8 (evdev codes start at 8)
  xkb_keycode_t xkb_keycode = key_code + 8;

  if (key_state == LIBINPUT_KEY_STATE_PRESSED) {

    direction = XKB_KEY_DOWN;

    xkb_keysym_t keysym =
        xkb_state_update_key(xkb_state, xkb_keycode, direction);

    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, xkb_keycode);

    uint32_t unicode = xkb_keysym_to_utf32(sym);

    if (is_drm_rendering) {

      if (unicode) {
        // printf("Key pressed: %c (U+%04x)\n", (char)unicode, unicode);
        if (unicode == 'd') {
          printf("Calling program\n");
          // call_program("/root/pterminal/test_terminal");
          call_program("firefox");
        }
        if (unicode == 'q') {
          printf("Calling program\n");
          exit(0);
        }
        if (unicode == 'w') {
          libseat_switch_session(compositor.seat, 3);
        }
      }
    }

  } else {                  // LIBINPUT_KEY_STATE_RELEASED
    direction = XKB_KEY_UP;
    xkb_state_update_key(xkb_state, xkb_keycode, direction);
  }

}

void init_xkb(void) {

  xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_context) {
    fprintf(stderr, "Failed to create XKB context\n");
    return;
  }

  // US layout)

  struct xkb_rule_names names = {
      .rules = "evdev",
      .model = "pc105",
      .layout = "us",
      .variant = NULL, // Or specify a variant like "dvorak"
      .options = NULL, // Or specify options like "terminate:ctrl_alt_bksp"
  };

  xkb_keymap =
      xkb_keymap_new_from_names(xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!xkb_keymap) {
    fprintf(stderr, "Failed to compile XKB keymap\n");
    xkb_context_unref(xkb_context);
    return;
  }

  xkb_state = xkb_state_new(xkb_keymap);
  if (!xkb_state) {
    fprintf(stderr, "Failed to create XKB state\n");
    xkb_keymap_unref(xkb_keymap);
    xkb_context_unref(xkb_context);
    return;
  }
}

void finish_keyboard(){
    xkb_state_unref(xkb_state);
    xkb_keymap_unref(xkb_keymap);
    xkb_context_unref(xkb_context);
}

void init_keyboard() {
  
  init_xkb();

}
