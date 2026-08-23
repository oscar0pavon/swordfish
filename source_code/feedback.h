#ifndef FEEDBACK_H
#define FEEDBACK_H

#define _GNU_SOURCE
#include "compositor.h"

void send_format_table(WResource* resource);

void get_feedback(WClient *client, WResource *resource,
    uint32_t id);

void get_surface_feedback(WClient *client,
				     WResource *resource,
				     uint32_t id,
             WResource *surface);
#endif
