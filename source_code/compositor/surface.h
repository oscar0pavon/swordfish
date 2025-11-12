#ifndef SURFACE_H
#define SURFACE_H

#include "compositor.h"

void create_surface(WaylandClient *client, WaylandResource *resource,
                    uint32_t id);
#endif
