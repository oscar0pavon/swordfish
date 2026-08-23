#ifndef DRM_FORMAT_H
#define DRM_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

//a DRM fourcc and the vulkan format that has the same bytes in the same order.
//the two naming schemes read backwards from each other: a fourcc names its
//channels from the top of a little endian 32 bit word down, vulkan names them
//in memory order. XR24 is X,R,G,B in the word and so B,G,R,A in memory, which
//is VK_FORMAT_B8G8R8A8
typedef struct DrmFormat {
  uint32_t fourcc;
  VkFormat vulkan;
  //X formats leave the fourth byte undefined, so the alpha the sampler reads
  //is whatever the client happened to leave there
  bool opaque;
  const char *name;
} DrmFormat;

//NULL when the fourcc has no vulkan format with the same byte order. several
//common ones do not - BGRA8888 is A,R,G,B in memory and vulkan has nothing
//that matches - and importing one anyway rotates every channel
const DrmFormat *drm_format_find(uint32_t fourcc);

const DrmFormat *drm_format_get(int index);
int drm_format_count(void);

#endif
