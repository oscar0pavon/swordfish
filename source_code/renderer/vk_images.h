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
  VkImage *texture_image;
  VkDeviceMemory *image_memory;
  VkSampleCountFlagBits number_of_samples;
} PImageCreateInfo;


extern VkImage pe_vk_texture_image;
extern VkImageView pe_vk_texture_image_view;
extern VkSampler pe_vk_texture_sampler;
extern VkImage pe_vk_depth_image;
extern VkDeviceMemory pe_vk_depth_image_memory;
extern VkImageView pe_vk_depth_image_view;

extern VkDeviceMemory pe_vk_texture_image_memory;

extern uint32_t pe_vk_mip_levels;

void pe_vk_create_texture_image();

void pe_vk_create_image(PImageCreateInfo *info);
void pe_vk_create_depth_resources();
void pe_vk_create_texture_sampler();
