#ifndef VK_BUFFER
#define VK_BUFFER
#include "vulkan.h"

typedef struct PBufferCreateInfo {
  VkDeviceSize size;
  VkBufferUsageFlags usage;
  VkMemoryPropertyFlags properties;
  VkBuffer buffer;
  VkDeviceMemory buffer_memory;
} PBufferCreateInfo;

void pe_vk_buffer_create(PBufferCreateInfo *buffer_info);
void pe_vk_copy_buffer(VkBuffer source, VkBuffer destination,
                       VkDeviceSize size);

VkBuffer pe_vk_create_buffer(Array *array, VkBufferUsageFlagBits type);
#endif
