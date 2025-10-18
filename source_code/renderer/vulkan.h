#ifndef PEVULKAN_H
#define PEVULKAN_H

#include <vulkan/vulkan_core.h>

#include <vulkan/vulkan.h>
#include <engine/array.h>
#include <engine/macros.h>

#include "cglm/mat4.h"

#include <engine/log.h>

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

Camera main_camera;

int pe_vk_init();
void pe_vk_end();

VkInstance vk_instance;
VkPhysicalDevice vk_physical_device;
VkDevice vk_device;
VkQueue vk_queue;

VkSurfaceKHR vk_surface;


VkRenderPass pe_vk_render_pass;

VkSampleCountFlagBits pe_vk_msaa_samples;

uint32_t q_graphic_family;
uint32_t q_present_family;


bool pe_vk_validation_layer_enable;

bool pe_vk_initialized;

VkImage pe_vk_color_image;
VkDeviceMemory pe_vk_color_memory;
VkImageView pe_vk_color_image_view;


#endif
