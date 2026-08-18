#ifndef WINDOW_H
#define WINDOW_H

#include <stdbool.h>

#define WINDOW_WIDTH 1916
#define WINDOW_HEIGHT 1040

extern bool swordfish_running;

//true when the window came from pway, so swordfish is a wayland client of the
//host compositor. the alternative is is_drm_rendering, driving KMS directly
extern bool is_wayland_window;

bool create_wayland_window(void);

void close_wayland_window(void);

#endif
