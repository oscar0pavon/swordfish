#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

//is_wayland_window and is_drm_rendering are the renderer's, out of
//<renderer/renderer.h> - the swap chain and the surface both read them

#define WINDOW_WIDTH 1916
#define WINDOW_HEIGHT 1040

extern bool swordfish_running;

bool create_wayland_window(void);

void close_wayland_window(void);

#endif
