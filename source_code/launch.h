#ifndef LAUNCH_H
#define LAUNCH_H

//fire and forget, does not wait for the program to exit. argv[0] is the
//program, NULL-terminated exactly as execvp wants it
void launch_program(char* const argv[]);

//SIGTERM every program launched this way, and the process groups they lead.
//without it they are init's children and outlive the compositor
void launch_close_programs();

#endif
