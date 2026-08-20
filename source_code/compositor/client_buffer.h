#ifndef CLIENT_BUFFER_H
#define CLIENT_BUFFER_H

#include <engine/images.h>
#include <engine/renderer/vk_buffer.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "types.h"

//which protocol a wl_buffer arrived on. the wl_buffer's user data is all
//surface_attach() has to go on, and the two paths used to put different structs
//behind that one void *: dma.c a PTexture, shared_memory.c its own bookkeeping
//struct. surface_attach() assumed the first unconditionally, so an shm buffer
//was read as a PTexture - which is nearly twice the size, so the memcpy into
//the quad ran off the end of the allocation and the quad got a VkImage handle
//made of whatever heap followed it
typedef enum ClientBufferType {
  CLIENT_BUFFER_DMA,
  CLIENT_BUFFER_SHARED_MEMORY
} ClientBufferType;

typedef struct SharedMemoryPool SharedMemoryPool;

typedef struct ClientBuffer {
  //first member of both, so the kind can be read off any buffer before
  //anything else in it is believed
  ClientBufferType type;

  //what the quad samples. a dmabuf is imported once and sampled zero-copy out
  //of the client's own memory; an shm buffer is copied into this
  PTexture texture;

  //the rest is shm only. the pixels belong to the client and it rewrites them
  //whenever it likes, so the pool is read at upload time rather than kept as a
  //pointer to the pixels - a resize remaps it and the mapping may move
  SharedMemoryPool *pool;
  int32_t offset;
  int32_t width;
  int32_t height;
  int32_t stride;
  VkFormat format;
  //an X format leaves the fourth byte undefined, so the view answers 1 for
  //alpha rather than sampling whatever the client left there
  bool opaque;

  //the client has drawn since the last copy. set by the compositor thread on
  //attach and commit, cleared by the render thread when the copy is made
  bool needs_upload;

  //the host visible buffer the pixels are staged through on their way to the
  //image. allocated once and rewritten, not made per upload: a window this size
  //is eight megabytes, and a fresh vkAllocateMemory every frame costs more than
  //the copy does
  PBuffer staging;
  bool has_staging;
} ClientBuffer;

#endif
