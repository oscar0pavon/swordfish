#ifndef RENDER_VK_SWAPCHAIN
#define RENDER_VK_SWAPCHAIN

#include <vulkan/vulkan.h>
#include <engine/array.h>

void pe_vk_swch_create();

extern VkSwapchainKHR pe_vk_swap_chain;

extern VkFormat pe_vk_swch_format;
extern VkExtent2D pe_vk_swch_extent;


extern VkImage pe_vk_swch_images[4];

#endif
