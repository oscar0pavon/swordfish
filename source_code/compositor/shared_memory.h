#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "client_buffer.h"

void init_shared_memory(void);

//copy the client's pixels into the buffer's own vulkan image, creating that
//image on the first call. must run on the render thread: it submits to vk_queue
//and waits on it, and a VkQueue is not something two threads may touch at once.
//end_frame() is where it is called from, once pe_vk_draw_frame() has drained
//the queue. false if the buffer cannot be put on the gpu at all
bool shared_memory_upload(ClientBuffer *buffer);

//destroy the vulkan images of shm buffers whose wl_buffer has gone. the render
//thread may be recording with one at the moment the client destroys it, so the
//destroy is queued there and paid here, on the render thread and after the
//queue is idle
void shared_memory_collect_textures(void);

#endif
