#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "input.h"

void init_keyboard(void);
void handle_xkb_keyboard_event(InputEvent *event);
void finish_keyboard(void);

#endif
