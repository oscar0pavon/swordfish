#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "client_buffer.h"

void init_shared_memory(void);

//copy the client's pixels into the buffer's own vulkan image, creating that
//image on the first call. must run on the render thread: it submits to vk_queue
//and waits on it, and a VkQueue is not something two threads may touch at once.
//begin_frame() is where it is called from, before pe_frame_draw() records a
//quad that samples the image. false if the buffer cannot be put on the gpu at
//all
bool shared_memory_upload(ClientBuffer *buffer);

//the vulkan images of destroyed wl_buffers used to be collected by this file's
//own one-frame list; they go through retire.c now, which counts the
//frames in flight - retire_collect() in end_frame() is what drains it

#endif
