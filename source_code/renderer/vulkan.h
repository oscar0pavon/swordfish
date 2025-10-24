#ifndef PEVULKAN_H
#define PEVULKAN_H

#include <vulkan/vulkan_core.h>
#define VK_USE_PLATFORM_XLIB_KHR // Must be defined before including vulkan.h
#include <vulkan/vulkan.h>

#include <engine/array.h>
#include <engine/macros.h>

#include <engine/log.h>

#include "cglm/cglm.h"

#define VEC3(p1, p2, p3)                                                       \
  (vec3) { p1, p2, p3 }
#define VEC4(p1, p2, p3, p4)                                                   \
  (vec4) { p1, p2, p3, p4 }

#define VEC2(p1, p2)                                                           \
  (vec2) { p1, p2 }

#define VKVALID(f,message) if(f != VK_SUCCESS){LOG("%s \n",message);}

typedef struct Camera{
    mat4 projection;
    mat4 view;
    vec3 front;
    vec3 up;
    vec3 position;
}Camera;

typedef struct PUniformBufferObject {
  mat4 model;
  mat4 view;
  mat4 projection;
  vec4 light_position;
} PUniformBufferObject;


int pe_vk_init();
void pe_vk_end();

  
extern VkPhysicalDevice vk_physical_device;


extern VkInstance vk_instance;
extern VkDevice vk_device;
extern VkQueue vk_queue;

extern VkSurfaceKHR vk_surface;


extern VkRenderPass pe_vk_render_pass;

extern VkSampleCountFlagBits pe_vk_msaa_samples;

extern uint32_t q_graphic_family;
extern uint32_t q_present_family;


extern bool pe_vk_validation_layer_enable;

extern bool pe_vk_initialized;

extern VkImage pe_vk_color_image;
extern VkDeviceMemory pe_vk_color_memory;
extern VkImageView pe_vk_color_image_view;

extern Camera main_camera;

extern VkViewport viewport;
extern VkRect2D scissor;

#endif
