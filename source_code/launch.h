#ifndef LAUNCH_H
#define LAUNCH_H

//fire and forget, does not wait for the program to exit
void launch_program(const char* command);

//SIGTERM every program launched this way, and the process groups they lead.
//without it they are init's children and outlive the compositor
void launch_close_programs();

#endif
