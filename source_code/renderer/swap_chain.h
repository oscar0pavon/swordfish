#ifndef RENDER_VK_SWAPCHAIN
#define RENDER_VK_SWAPCHAIN

#include <vulkan/vulkan.h>
#include <engine/array.h>

void pe_vk_create_swapchain();

extern VkSwapchainKHR pe_vk_swap_chain;

extern VkFormat pe_vk_swch_format;
extern VkExtent2D pe_vk_swch_extent;

extern u32 pe_vk_swapchain_image_count;

extern VkImage pe_vk_swch_images[4];
extern VkImage pe_vk_exportable_images[4];

#endif
