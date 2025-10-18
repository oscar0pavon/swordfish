#ifndef RENDER_VK_SWAPCHAIN
#define RENDER_VK_SWAPCHAIN

#include <vulkan/vulkan.h>
#include <engine/array.h>

void pe_vk_swch_create();

static VkSwapchainKHR pe_vk_swap_chain;

static VkFormat pe_vk_swch_format;
static VkExtent2D pe_vk_swch_extent;


static VkImage pe_vk_swch_images[4];

#endif
