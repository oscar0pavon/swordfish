#ifndef COMPOSITOR_OUTPUT_H
#define COMPOSITOR_OUTPUT_H

#include "types.h"

//version 4, for wl_output.name and wl_output.description. the only request the
//interface has in any version is release, and that has a handler below, so
//there is no NULL entry for libwayland to dispatch as a call
#define OUTPUT_VERSION 4

void init_output();

//tell a client which output its window is on. a toolkit that picks its scale
//from the output it landed on waits for this before it draws anything, so a
//window that never gets it never maps. output_index is an index into
//swordfish_outputs - see outputs.h
void output_send_surface_enter(WResource *surface_resource, int output_index);

#endif
