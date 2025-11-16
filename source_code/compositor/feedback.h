#ifndef FEEDBACK_H
#define FEEDBACK_H

#define _GNU_SOURCE
#include "compositor.h"

void send_format_table(WaylandResource* resource);

void get_feedback(WaylandClient *client, WaylandResource *resource,
    uint32_t id);

void get_surface_feedback(WaylandClient *client,
				     WaylandResource *resource,
				     uint32_t id,
             WaylandResource *surface);
#endif
