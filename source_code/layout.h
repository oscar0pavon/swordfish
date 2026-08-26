#ifndef LAYOUT_H
#define LAYOUT_H

#include "tasks.h"

//the window manager half of the compositor: which window gets which piece of
//the output, which one has the keyboard, and closing one. nothing here talks
//to the gpu - it writes a rectangle onto every Task and draw_surface() puts
//the quad where it says
void layout_apply(void);

//move the keyboard focus along the layout order. +1 is the next window, -1 the
//previous. nothing moves: the spiral is in map order and a window keeping its
//cell while the focus walks over it is the point
void layout_focus_next(int direction);

//give the keyboard to a surviving window when the one holding it is gone.
//a no-op while the focus is still on a live window, so it is safe to call from
//every teardown path
void layout_focus_fallback(void);

void layout_close_focused(void);

//pull a floating window to the front of the stacking order among floats.
//a no-op on a tiled window - see the comment on the definition
void layout_raise(Task *task);

//flip the focused window between tiled and floating
void layout_toggle_floating(void);

#endif
