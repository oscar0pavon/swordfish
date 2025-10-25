#ifndef VK_IMAGES_H
#define VK_IMAGES_H

#include "engine/images.h"
#include "vulkan.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>


typedef struct PImageCreateInfo {
  uint32_t width;
  uint32_t height;
  uint32_t mip_level;
  VkFormat format;
  VkImageTiling tiling;
  VkImageUsageFlags usage;
  VkMemoryPropertyFlags properties;
  PTexture* texture;
  VkSampleCountFlagBits number_of_samples;
  bool is_exportable;
} PImageCreateInfo;


extern VkImageView pe_vk_depth_image_view;


void pe_vk_create_texture(PTexture* new_texture, const char* path);

void pe_vk_create_image(PImageCreateInfo *info);
void pe_vk_create_depth_resources();

void pe_vk_create_exportable_images();

#endif
