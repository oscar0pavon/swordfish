#include "window.h"

#include <stdio.h>

#include <pway/pway.h>

#include "keyboard.h"
#include "swordfish.h"

bool swordfish_running = true;

bool is_wayland_window = false;

static void pway_window_closed(void) {
  swordfish_running = false;
}

//the raw key from the host compositor, not pway->input's utf8: swordfish has
//to pass these on to its own clients, and text carries neither the keycode nor
//the release
static void pway_window_key(uint32_t key_code, uint32_t state) {
  handle_key_code(key_code, state == WL_KEYBOARD_KEY_STATE_PRESSED);
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
