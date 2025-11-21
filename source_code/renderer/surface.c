#include "surface.h"

#include "vulkan.h"
#include "vk_images.h"
#include "swap_chain.h"
#include "../window.h"
#include "../swordfish.h"
#include "images_view.h"

VkImage pe_vk_color_image;
VkDeviceMemory pe_vk_color_memory;
VkImageView pe_vk_color_image_view;

VkSurfaceKHR vk_surface;

PTexture vk_color_image;

void pe_vk_create_surface() {

  if (!is_drm_rendering) {

    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = swordfish_window,
    };

    if (vkCreateXlibSurfaceKHR(vk_instance, &surfaceCreateInfo, NULL,
                               &vk_surface) != VK_SUCCESS) {
      fprintf(stderr, "Failed to create Vulkan Xlib surface!\n");
    }
  }
}

void pe_vk_create_color_resources() {
  VkFormat color_format = pe_vk_swch_format;

  vk_color_image.mip_level = 1;
  PImageCreateInfo image_create_info = {
      .width = pe_vk_swch_extent.width,
      .height = pe_vk_swch_extent.height,
      .texture = &vk_color_image,
      .format = color_format,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .mip_level = 1,
      .number_of_samples = pe_vk_msaa_samples};

  pe_vk_create_image(&image_create_info);

  pe_vk_color_image_view = pe_vk_create_image_view(
      vk_color_image.image, color_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void pe_vk_set_viewport_and_sccisor(){
  //TODO update when recreate swap chain for handling resizing window

  VkOffset2D offset = {0, 0};

  scissor.extent = pe_vk_swch_extent;
  scissor.offset = offset;

  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = (float)pe_vk_swch_extent.width;
  viewport.height = (float)pe_vk_swch_extent.height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

}

