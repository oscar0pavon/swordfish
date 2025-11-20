#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "input.h"

extern struct xkb_context *xkb_context;
extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

void init_keyboard(void);
void handle_xkb_keyboard_event(InputEvent *event);
void finish_keyboard(void);


off_t get_keymap_file_size(int fd);

int create_keymap_file_descriptor(off_t *size_out);

#endif
