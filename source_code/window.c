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

//pway hands us the utf8 the host compositor's keymap produced. without this
//the shortcuts only ever existed on the libinput path, so a windowed
//swordfish had no keys of its own at all
static void pway_window_input(const char *text, int length) {
  for (int index = 0; index < length; index++)
    handle_swordfish_key((unsigned char)text[index]);
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
  pway->input = pway_window_input;

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
