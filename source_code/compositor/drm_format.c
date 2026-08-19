#include "drm_format.h"

#include <drm/drm_fourcc.h>
#include <stddef.h>

//the 8 bit formats are sRGB: that is what a wayland client renders into its
//buffer unless a colour management protocol says otherwise, and swordfish's
//swapchain is VK_FORMAT_B8G8R8A8_SRGB. sampling them as UNORM hands the
//framebuffer a value that is already gamma encoded and lets it encode the
//whole thing a second time, which is what made client windows look washed out
//next to the rest of the scene - every texture the engine loads is _SRGB too.
//
//INFO the 10 and 16 bit formats are deliberately absent. vulkan has no
//A2R10G10B10_SRGB or R16G16B16A16_SRGB, so there is no hardware sRGB decode on
//the way in and the double encoding comes straight back. advertising them is
//also not free: mesa picked XR30 the moment it was offered, because a client
//that does not ask for a specific depth takes the first config that matches,
//so offering 10 bit quietly moved every client onto a path that renders wrong
static const DrmFormat drm_formats[] = {
    {DRM_FORMAT_XRGB8888, VK_FORMAT_B8G8R8A8_SRGB, true, "XR24"},
    {DRM_FORMAT_ARGB8888, VK_FORMAT_B8G8R8A8_SRGB, false, "AR24"},
    {DRM_FORMAT_XBGR8888, VK_FORMAT_R8G8B8A8_SRGB, true, "XB24"},
    {DRM_FORMAT_ABGR8888, VK_FORMAT_R8G8B8A8_SRGB, false, "AB24"},
};

#define DRM_FORMAT_COUNT ((int)(sizeof(drm_formats) / sizeof(drm_formats[0])))

const DrmFormat *drm_format_find(uint32_t fourcc) {
  for (int i = 0; i < DRM_FORMAT_COUNT; i++) {
    if (drm_formats[i].fourcc == fourcc)
      return &drm_formats[i];
  }

  return NULL;
}

const DrmFormat *drm_format_get(int index) {
  if (index < 0 || index >= DRM_FORMAT_COUNT)
    return NULL;

  return &drm_formats[index];
}

int drm_format_count(void) { return DRM_FORMAT_COUNT; }
