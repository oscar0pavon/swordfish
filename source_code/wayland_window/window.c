#include "window.h"

#include <stdio.h>
#include <linux/input-event-codes.h>

#include <pway/pway.h>

#include "keyboard.h"
#include "mouse.h"
#include "swordfish.h"

bool swordfish_running = true;

static void pway_window_closed(void) {
  swordfish_running = false;
}

//the raw key from the host compositor, not pway->input's utf8: swordfish has
//to pass these on to its own clients, and text carries neither the keycode nor
//the release
static void pway_window_key(uint32_t key_code, uint32_t state) {
  handle_key_code(key_code, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

//the host reports the cursor in the window's own pixels, and the window is not
//the size swordfish renders at: resizing is not implemented, so a tiled window
//is handed a WINDOW_WIDTH x WINDOW_HEIGHT image scaled into whatever size it
//actually got. the cursor has to be scaled the same way or it lands somewhere
//other than where the user is pointing
static double scale_to_render_size(double value, int window_size,
                                   int render_size) {

  if (window_size <= 0)
    return value;

  return value * (double)render_size / window_size;
}

static void pway_window_mouse_moved(void) {
  move_cursor_to(
      scale_to_render_size(pway->mouse.x, pway->width, WINDOW_WIDTH),
      scale_to_render_size(pway->mouse.y, pway->height, WINDOW_HEIGHT));
}

//pway hands over which of its own buttons moved, not the evdev code that has
//to go back out to the client
static uint32_t pway_window_button_code(void) {

  PMouse *mouse = &pway->mouse;

  if (mouse->current_button == &mouse->left_button)
    return BTN_LEFT;

  if (mouse->current_button == &mouse->middle_button)
    return BTN_MIDDLE;

  if (mouse->current_button == &mouse->right_button)
    return BTN_RIGHT;

  return 0;
}

static void pway_window_click(void) {

  uint32_t button = pway_window_button_code();

  //pway calls this for anything it does not recognise as well, with the button
  //left NULL
  if (button)
    handle_pointer_button(button, true);
}

static void pway_window_click_release(void) {

  PMouse *mouse = &pway->mouse;

  //the wheel comes through the release callback rather than a scroll one, and
  //pway labels a negative axis value - which the protocol means as scrolling
  //up - wheel_down, so the names are the opposite way round from the sign that
  //has to be sent on
  if (mouse->current_button == &mouse->wheel_down) {
    handle_pointer_axis(-SCROLL_STEP);
    return;
  }

  if (mouse->current_button == &mouse->wheel_up) {
    handle_pointer_axis(SCROLL_STEP);
    return;
  }

  uint32_t button = pway_window_button_code();

  if (button)
    handle_pointer_button(button, false);
}

static void pway_window_resized(int width, int height) {
  //TODO the swap chain and the camera still use WINDOW_WIDTH/WINDOW_HEIGHT,
  //so the new size is recorded but nothing is rebuilt from it yet
  pway->width = width;
  pway->height = height;
}

//pway connects with whatever WAYLAND_DISPLAY points at, and run_compositor()
//later overwrites that with swordfish's own socket. this has to run before
//the compositor thread starts or swordfish tries to be a client of itself
bool create_wayland_window(void) {

  if (pway_init() == NULL)
    return false;

  pway->exit = pway_window_closed;
  pway->resize = pway_window_resized;
  pway->key = pway_window_key;

  //pway registers a pointer listener whenever the host has one, and calls
  //these from it. left alone they are its own no-op defaults, which is what
  //swallowed every mouse event until now
  pway->update_mouse = pway_window_mouse_moved;
  pway->click = pway_window_click;
  pway->click_release = pway_window_click_release;

  if (!pway_create_window("swordfish", WINDOW_WIDTH, WINDOW_HEIGHT))
    return false;

  //deliberately no pway_init_egl(): the wl_surface goes to vulkan instead, so
  //the EGL context pway would build is never needed
  is_wayland_window = true;

  return true;
}

void close_wayland_window(void) {
  pway_finish();
}
