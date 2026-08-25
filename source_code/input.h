#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H


#include "types.h"
#include "tasks.h"


extern bool focus_entered;
extern Task *keyboard_focus;

//milliseconds from CLOCK_MONOTONIC, the timebase every event timestamp the
//compositor sends is in
uint32_t get_current_time_msec();

void init_compositor_input();

void handle_focus();

//one key from whichever input path is live, already an evdev code. pressed
//keys are tracked here so a client taking focus is told what is already down
void send_wayland_key(uint32_t scancode, bool pressed);

//release every key still held, to the focused client and to the compositor's
//own xkb state. the VT switch calls it: the releases of the keys that asked
//for the switch are delivered to the VT we left, not to us
void input_release_pressed_keys(void);

//the cursor, in the render target's own pixels, from whichever input path is
//live. the surface under it is worked out here, so motion is what sends
//wl_pointer.enter and wl_pointer.leave as well
void send_wayland_pointer_motion(double x, double y);

//an evdev BTN_* code, which is what the protocol carries as well
void send_wayland_pointer_button(uint32_t button, bool pressed);

//vertical scroll. positive is down, away from the user, as the protocol has it
void send_wayland_pointer_axis(double value);

//wl_keyboard.leave the old surface, wl_keyboard.enter the new one -
//focus_task() is its only caller
void set_keyboard_focus(struct Task *task);

//the client holding the keyboard, or NULL if no window has been entered yet.
//the selection goes to whoever this is
WClient *keyboard_focus_client(void);

//called before a Task or a TaskInput is freed, so nothing keeps a pointer to it
void forget_task(struct Task *task);
void forget_task_input(TaskInput *input);

#endif
