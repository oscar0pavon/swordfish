#ifndef SURFACE_H
#define SURFACE_H

#include "compositor.h"

#include "engine/array.h"

extern Array surface_to_draw;

void create_surface(WClient *client, WResource *resource,
                   uint32_t id);



#endif
