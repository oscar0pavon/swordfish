#ifndef MOUSE_H
#define MOUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "input.h"

//one wheel click. the protocol measures scroll in the same units as surface
//coordinates and clients read 15 as a notch, which is also libinput's degrees
//per click, so the two paths agree on what a click is worth
#define SCROLL_STEP 15.0

//where the cursor is, in the render target's own pixels - the space the client
//quads are drawn in, not the space the window is displayed at
extern double cursor_x, cursor_y;

//the host compositor gives an absolute position inside its window, libinput on
//bare drm gives how far the mouse moved. both end up in the same cursor
void move_cursor_to(double x, double y);
void move_cursor_by(double dx, double dy);

//an evdev BTN_* code, which is what the protocol carries as well
void handle_pointer_button(uint32_t button, bool pressed);

//vertical scroll, positive downwards as the protocol has it
void handle_pointer_axis(double value);

//motion, buttons and scroll off a libinput event, for the drm path
void handle_libinput_pointer_event(InputEvent *event);

#endif
