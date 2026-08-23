#ifndef DEVICE_INPUT_H
#define DEVICE_INPUT_H

#include <libinput.h>
typedef struct libinput LibInput;
typedef struct libinput_event InputEvent;
typedef struct libinput_event_keyboard InputEventKeyboard;

void* handle_input(void* none);

//declared by input.h (the wl_seat one), which owns it

void init_input();

void finish_input();

extern LibInput* libinput;

#endif
