#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H


#include "types.h"

//defined in compositor.h, which includes this header before it gets there
struct Task;

typedef struct TaskInput {
  WClient *client;
  WResource *resource;
  WResource *keyboard_resource;
  struct wl_list link;
}TaskInput;

void init_compositor_input();

void handle_focus();

//one key from whichever input path is live, already an evdev code. pressed
//keys are tracked here so a client taking focus is told what is already down
void send_wayland_key(uint32_t scancode, bool pressed);

//wl_keyboard.leave the old surface, wl_keyboard.enter the new one. must be
//called with focus_task_mutex held - focus_task() is its only caller
void set_keyboard_focus(struct Task *task);

//called before a Task or a TaskInput is freed, so nothing keeps a pointer to it
void forget_task(struct Task *task);
void forget_task_input(TaskInput *input);

#endif
