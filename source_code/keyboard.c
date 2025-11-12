#include "keyboard.h"
#include "input.h"
#include <stdio.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

struct xkb_context *xkb_context;
struct xkb_keymap *xkb_keymap;
struct xkb_state *xkb_state;

void handle_xkb_keyboard_event(InputEvent *event) {
  InputEventKeyboard *key_event = libinput_event_get_keyboard_event(event);

  uint32_t key_code = libinput_event_keyboard_get_key(key_event);

  enum libinput_key_state key_state =
      libinput_event_keyboard_get_key_state(key_event);

  enum xkb_key_direction direction;

  xkb_keycode_t xkb_keycode = key_code + 8;

  if (key_state == LIBINPUT_KEY_STATE_PRESSED) {

    direction = XKB_KEY_DOWN;

    xkb_keysym_t keysym =
        xkb_state_update_key(xkb_state, xkb_keycode, direction);

    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, xkb_keycode);
    uint32_t unicode = xkb_keysym_to_utf32(sym);

    if (unicode) {
      printf("Key pressed: %c (U+%04x)\n", (char)unicode, unicode);
    }
  } else {                  // LIBINPUT_KEY_STATE_RELEASED
    direction = XKB_KEY_UP; // Use XKB_KEY_UP for released
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
