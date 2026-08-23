#ifndef DEVICE_INPUT_H
#define DEVICE_INPUT_H

#include <libinput.h>
typedef struct libinput LibInput;
typedef struct libinput_event InputEvent;
typedef struct libinput_event_keyboard InputEventKeyboard;

//drains and dispatches whatever libinput already has queued; the caller must
//already know the libinput fd is readable
void dispatch_libinput_events(void);

//declared by input.h (the wl_seat one), which owns it

void init_input();

void finish_input();

extern LibInput* libinput;

#endif
