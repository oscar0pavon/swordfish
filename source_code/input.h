#ifndef INPUT_H
#define INPUT_H

#include <libinput.h>
typedef struct libinput LibInput;
typedef struct libinput_event InputEvent;
typedef struct libinput_event_keyboard InputEventKeyboard;

void* handle_input(void* none);

//declared by compositor/input.h, which owns it

void init_input();

void finish_input();

extern LibInput* libinput;

#endif
