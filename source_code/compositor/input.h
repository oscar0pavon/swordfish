#ifndef COMPOSITOR_INPUT_H
#define COMPOSITOR_INPUT_H


#include "types.h"

typedef struct TaskInput {
  WClient *client;
  WResource *resource;
  WResource *keyboard_resource;
  struct wl_list link;
}TaskInput;

void init_compositor_input();

void handle_focus();

#endif
