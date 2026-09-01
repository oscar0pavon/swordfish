#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include "device_input.h"

extern struct xkb_context *xkb_context;
extern struct xkb_keymap *xkb_keymap;
extern struct xkb_state *xkb_state;

void init_keyboard(void);
void handle_xkb_keyboard_event(InputEvent *event);

//one key as an evdev code, from libinput. runs the compositor's own super
//shortcuts and passes everything else to the focused client
void handle_key_code(uint32_t key_code, bool pressed);
void finish_keyboard(void);

//is the compositor's own modifier down right now. pointer.c reads this too -
//super+drag moves and resizes a floating window the same way super+key runs
//one of the shortcuts below
bool super_key_held(void);


off_t get_keymap_file_size(int fd);

int create_keymap_file_descriptor(off_t *size_out);

#endif
