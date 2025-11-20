#include "vulkan.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "descriptor_set.h"
#include "direct_render.h"
#include "engine/array.h"
#include "engine/images.h"
#include "framebuffer.h"
#include "images_view.h"
#include "pipeline.h"
#include "render_pass.h"
#include "shader_module.h"
#include "swap_chain.h"
#include "swordfish.h"
#include "sync.h"
#include "uniform_buffer.h"
#include "vk_images.h"
#include "vk_vertex.h"
#include <engine/log.h>
#include <engine/macros.h>
#include <vulkan/vulkan_core.h>
#include <wchar.h>

#include "../window.h"

#include "display.h"
#include "physical_devices.h"
#include "logical_device.h"
#include "instance.h"
#include "debug.h"
#include "queues.h"

VkInstance vk_instance;
VkDevice vk_device;

PTexture vk_color_image;

VkSurfaceKHR vk_surface;

VkRenderPass pe_vk_render_pass;

VkSampleCountFlagBits pe_vk_msaa_samples;

VkViewport viewport;
VkRect2D scissor;


bool pe_vk_initialized;

VkImage pe_vk_color_image;
VkDeviceMemory pe_vk_color_memory;
VkImageView pe_vk_color_image_view;


PFN_vkGetMemoryFdKHR pe_vk_get_memory_file_descriptor;





Array buffers;





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

int pe_vk_init() {
  pe_vk_msaa_samples = VK_SAMPLE_COUNT_4_BIT;

  array_init(&buffers, sizeof(VkBuffer), 512);

  pe_vk_create_instance();

  pe_vk_get_physical_device();
  
  pe_vk_queue_families_support();

  pe_vk_create_logical_device();
 
  if(is_drm_rendering){
    pe_vk_get_memory_file_descriptor =
        (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(vk_device, "vkGetMemoryFdKHR");

    if(!pe_vk_get_memory_file_descriptor){
      printf("Error can't get vulkan extenstion\n");
      return 1;
    }
    vk_get_displays();
  }

  vkGetDeviceQueue(vk_device, q_graphic_family, 0, &vk_queue);

  pe_vk_create_surface();

  pe_vk_create_swapchain();

  if(is_drm_rendering){
    pe_vk_create_exportable_images();
    //init_direct_render();
  }

  pe_vk_set_viewport_and_sccisor();

  pe_vk_create_images_views();

  pe_vk_create_render_pass();

  pe_vk_create_descriptor_set_layout();
  
  pe_vk_create_descriptor_set_layout_with_texture();
  //pe_vk_create_descriptor_set_layout_skinned();

  
  pe_vk_pipeline_create_layout(true, &pe_vk_pipeline_layout_with_descriptors,
                               &pe_vk_descriptor_set_layout);

  pe_vk_pipeline_create_layout(true, &pe_vk_pipeline_layout3,
                               &pe_vk_descriptor_set_layout_with_texture);

  // pe_vk_pipeline_create_layout(true, &pe_vk_pipeline_layout_skinned,
  //                              &pe_vk_descriptor_set_layout_skinned);

  pe_vk_pipelines_init();

  pe_vk_initialized = true;

  pe_vk_commands_pool_init();

  pe_vk_create_color_resources();

  pe_vk_create_depth_resources();

  pe_vk_framebuffer_create();

  pe_vk_command_init();

  pe_vk_semaphores_create();


  LOG("Vulkan intialize [OK]\n");
  return 0;
}


void pe_vk_end() {

  pe_vk_clean_commands();

  vkDestroySwapchainKHR(vk_device, pe_vk_swap_chain, NULL);

  pe_vk_end_sync();

  pe_vk_clean_image(&vk_depth_image);
  pe_vk_clean_image(&vk_color_image);

  for(int i = 0; i < buffers.count; i++){
    VkBuffer* buffer = array_get(&buffers, i);
    vkDestroyBuffer(vk_device,*buffer,NULL);
  }
  pe_vk_debug_end();
  vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
  vkDestroyDevice(vk_device, NULL);
  vkDestroyInstance(vk_instance, NULL);
}
