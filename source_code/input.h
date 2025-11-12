#ifndef INPUT_H
#define INPUT_H

typedef struct libinput LibInput;

void* handle_input(void* none);

void init_input();

void finish_input();

extern LibInput* libinput;

#endif
