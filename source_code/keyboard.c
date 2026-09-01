#define _GNU_SOURCE
#include <stdint.h>
#include "keyboard.h"
#include "launch.h"
#include "compositor.h"
#include "layout.h"
#include "device_input.h"
#include "sword.h"
#include <complex.h>
#include <stdio.h>
#include <libinput.h>
#include "tty.h"
#include <fcntl.h>
#include <mman.h>
#include <string.h>
#include <unistd.h>
#include "input.h"
#include "log.h"
#include "wayland_window/window.h"

struct xkb_context *xkb_context;
struct xkb_keymap *xkb_keymap;
struct xkb_state *xkb_state;

//return file descriptor of keymap
int create_keymap_file_descriptor(off_t *size_out){
  if(!xkb_keymap){
    log_error("No xkb keymap, init_keyboard() has not run");
    return -1;
  }

  char *keymap_string = xkb_keymap_get_as_string(xkb_keymap, 
      XKB_KEYMAP_FORMAT_TEXT_V1);
  if(!keymap_string){
    log_error("Failed to get XKB keymap string");
    return -1;
  }

  size_t size = strlen(keymap_string);

  int fd = memfd_create("sword-keyboard", MFD_CLOEXEC);
  if(fd < 0){
    log_error("Can't create file descriptor for keyboard");
    free(keymap_string);
    return -1;
  }

  if(ftruncate(fd, size) < 0){
   log_error("Can't truncate keyboard file descriptor");
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




//where super+w goes. a fixed VT rather than a cycle, the same one the libseat
//call it replaces asked for - change it here if the compositor to come back to
//lives somewhere else
#define SWITCH_TO_VT_NUMBER 3

//keys the compositor ate on the way down. their release has to be eaten too,
//or the client is handed a release for a press it never saw
#define MAX_SWALLOWED_KEYS 8
static uint32_t swallowed_keys[MAX_SWALLOWED_KEYS];
static int swallowed_keys_count;

static bool take_swallowed_key(uint32_t key_code) {
  for (int i = 0; i < swallowed_keys_count; i++) {
    if (swallowed_keys[i] != key_code)
      continue;

    swallowed_keys[i] = swallowed_keys[--swallowed_keys_count];
    return true;
  }

  return false;
}

static void swallow_key(uint32_t key_code) {
  if (swallowed_keys_count < MAX_SWALLOWED_KEYS)
    swallowed_keys[swallowed_keys_count++] = key_code;
}

//the shortcuts of the compositor itself, shared by both input paths: libinput
//on bare DRM, and the host compositor through pway when sword is a wayland
//client. returns whether the key was consumed
static bool handle_sword_key(uint32_t unicode) {

  switch (unicode) {
  case '\r':
    //must not block: this runs on the input thread, and in the pway path that
    //thread is also what pumps the window
    launch_program((char* const[]){"/root/pterminal/pterminal", NULL});
    return true;
  case 'm':
    //must not block: this runs on the input thread, and in the pway path that
    //thread is also what pumps the window
    launch_program((char* const[]){"/usr/bin/firefox", NULL});
    return true;
  case 'l':
    layout_focus_next(1);
    return true;
  case 'h':
    layout_focus_next(-1);
    return true;
  case 'c':
  case 'Q':
    //the focused window, not the compositor - shift+c below closes that
    layout_close_focused();
    return true;
  case ' ':
    layout_toggle_floating();
    return true;
  case 'C':
    //shift+c: closes sword itself, not the focused window - plain c above does that
    log_info("Closing from keyboard");
    sword_running = false;
    return true;
  case 'w':
    //there is only a VT to switch when we own the tty. this used to call
    //libseat_switch_session() on a seat that was never opened - init_seat()
    //had been commented out in main() - so the one shortcut that needed a
    //seat daemon was a NULL dereference. the kernel does the switching now
    if (is_drm_rendering)
      tty_switch_to(SWITCH_TO_VT_NUMBER);
    return true;
  }

  return false;
}

//shortcuts are behind super, so everything else belongs to the focused client.
//unconditional shortcuts meant a terminal could never type d, and typing q
//killed the compositor
bool super_key_held(void) {
  return xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_LOGO,
                                      XKB_STATE_MODS_EFFECTIVE) > 0;
}

//ctrl+alt is the VT switch, and it is deliberately not super: it is the
//gesture the kernel console itself answers, and the one every user already
//knows
static bool vt_switch_modifiers_held(void) {
  return xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL,
                                      XKB_STATE_MODS_EFFECTIVE) > 0 &&
         xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_ALT,
                                      XKB_STATE_MODS_EFFECTIVE) > 0;
}

//INFO the kernel's own ctrl+alt+Fn stops working the moment tty_silence_keyboard()
//puts the console keyboard in K_OFF - the driver delivers nothing at all, and
//that includes the switch it used to handle itself. so the compositor has to
//do the switching, which is what every VT owning compositor does anyway.
//returns the VT to go to, or 0 for a key that is not one
static int vt_number_for_keysym(xkb_keysym_t sym) {

  //some keymaps translate ctrl+alt+Fn into these themselves, and then the Fn
  //keysym below is never what arrives
  if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12)
    return sym - XKB_KEY_XF86Switch_VT_1 + 1;

  if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
    return sym - XKB_KEY_F1 + 1;

  //the number row as well as the function keys, since ctrl+alt+2 is the more
  //obvious spelling of the two. 0 is tty10: there is no tty0 to switch to,
  ///dev/tty0 is a name for whichever one is current
  if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9)
    return sym - XKB_KEY_1 + 1;

  if (sym == XKB_KEY_0)
    return 10;

  return 0;
}

//one key, whichever path it arrived on, as an evdev code
void handle_key_code(uint32_t key_code, bool pressed) {

  // XKB uses keycodes offset by 8 (evdev codes start at 8)
  xkb_keycode_t xkb_keycode = key_code + 8;

  //our own state goes first: the modifiers the client is sent and the super
  //test below both read it, and both were reading the previous key's state
  //while this lived after the send
  xkb_state_update_key(xkb_state, xkb_keycode,
                       pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

  if (!pressed) {
    if (take_swallowed_key(key_code))
      return;

    send_wayland_key(key_code, false);
    return;
  }

  //only when sword owns the tty. in a window the host compositor is the
  //one holding the VT, so the combination belongs to the focused client
  if (is_drm_rendering && vt_switch_modifiers_held()) {
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, xkb_keycode);
    int vt_number = vt_number_for_keysym(sym);

    if (vt_number) {
      log_info("Switching to VT %i", vt_number);
      tty_switch_to(vt_number);
      swallow_key(key_code);
      return;
    }
  }

  if (super_key_held()) {
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, xkb_keycode);
    uint32_t unicode = xkb_keysym_to_utf32(sym);

    if (unicode && handle_sword_key(unicode)) {
      swallow_key(key_code);
      return;
    }
  }

  send_wayland_key(key_code, true);
}

void handle_xkb_keyboard_event(InputEvent *event) {
  InputEventKeyboard *key_event = libinput_event_get_keyboard_event(event);

  enum libinput_key_state key_state =
      libinput_event_keyboard_get_key_state(key_event);

  uint32_t key_code = libinput_event_keyboard_get_key(key_event);

  handle_key_code(key_code, key_state == LIBINPUT_KEY_STATE_PRESSED);
}

void init_xkb(void) {

  xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_context) {
    log_error("Failed to create XKB context");
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
    log_error("Failed to compile XKB keymap");
    xkb_context_unref(xkb_context);
    return;
  }

  xkb_state = xkb_state_new(xkb_keymap);
  if (!xkb_state) {
    log_error("Failed to create XKB state");
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
