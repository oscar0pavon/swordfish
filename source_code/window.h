#ifndef WINDOW_H
#define WINDOW_H

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>


extern Display* display;
extern XEvent window_event;
extern Window swordfish_window; 

extern int mouse_click_x;
extern int mouse_click_y;

extern bool swordfish_running;

void create_window();

#endif
