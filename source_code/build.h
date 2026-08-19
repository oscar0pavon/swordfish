#ifndef BUILD_H
#define BUILD_H

void* call_make(void*none);

void call_program(const char* command);

//fire and forget, does not wait for the program to exit
void launch_program(const char* command);

#endif
