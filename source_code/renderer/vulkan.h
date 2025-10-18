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


int pe_vk_init();
void pe_vk_end();

static VkInstance vk_instance;
static VkPhysicalDevice vk_physical_device;
static VkDevice vk_device;
static VkQueue vk_queue;

static VkSurfaceKHR vk_surface;


static VkRenderPass pe_vk_render_pass;

static VkSampleCountFlagBits pe_vk_msaa_samples;

static uint32_t q_graphic_family;
static uint32_t q_present_family;


static bool pe_vk_validation_layer_enable;

static bool pe_vk_initialized;

static VkImage pe_vk_color_image;
static VkDeviceMemory pe_vk_color_memory;
static VkImageView pe_vk_color_image_view;

static Camera main_camera;

#endif
