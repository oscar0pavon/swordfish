#ifndef SURFACE_H
#define SURFACE_H

#include "compositor.h"

#include <engine/array.h>

extern Task *focused_task;
extern Array tasks_for_draw;

void create_surface(WClient *client, WResource *resource,
                   uint32_t id);

void send_frame_callback_done(Task *surface);

//the surface is a cursor image, not a window: keep it out of the draw list and
//out of everything that follows from being in it
void mark_surface_as_cursor(Task *task);

//hand back the buffer this surface stopped sampling when the client attached
//the next one. only safe once the gpu has finished the frame that read it, so
//end_frame() is what calls it
void task_release_old_buffer(Task *surface);

//copy an shm client's pixels onto the gpu, if it has drawn since the last time.
//submits to the queue, so it belongs on the render thread and nowhere else -
//end_frame() again, for the same reason
void task_upload_shared_memory(Task *surface);


#endif
