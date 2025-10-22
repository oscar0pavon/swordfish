#include "uniform_buffer.h"
#include "cglm/affine.h"
#include "cglm/types.h"
#include "cglm/vec4.h"

#include "engine/model.h"
#include "renderer/cglm/mat4.h"
#include "vk_buffer.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "../swordfish.h"


PUniformBufferObject ubo;

PBufferCreateInfo buffer_color;

static PUniformBufferObject cube_ubo;

void pe_vk_ubo_init() {
  ZERO(ubo);
  glm_mat4_identity(ubo.projection);
  glm_mat4_identity(ubo.view);
  glm_mat4_identity(ubo.model);
}

PBufferCreateInfo pe_vk_uniform_buffer_create_buffer(size_t size) {

  PBufferCreateInfo info;
  ZERO(info);
  info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  info.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  info.size = size;

  pe_vk_create_buffer_memory(&info);
  return info;
}

void pe_vk_create_uniform_buffers(PModel *model) {
  VkDeviceSize buffer_size = sizeof(PUniformBufferObject);

  array_init(&model->uniform_buffers, sizeof(VkBuffer), 4);
  array_init(&model->uniform_buffers_memory, sizeof(VkDeviceMemory), 4);

  for (int i = 0; i < 4; i++) {
    // create buffer
    PBufferCreateInfo info =
        pe_vk_uniform_buffer_create_buffer(sizeof(PUniformBufferObject));
    array_add(&model->uniform_buffers, &info.buffer);
    array_add(&model->uniform_buffers_memory, &info.buffer_memory);
  }

  // buffer_color = pe_vk_uniform_buffer_create_buffer(sizeof(PEColorShader));
}

void pe_vk_memory_copy(size_t size, VkDeviceMemory *memory, void *in_data) {

  void *data;
  vkMapMemory(vk_device, *(memory), 0, size, 0, &data);
  memcpy(data, in_data, size);
  vkUnmapMemory(vk_device, *(memory));
}

void pe_vk_send_uniform_buffer(PModel* model, uint32_t image_index){

  PUniformBufferObject buffers[] = {model->uniform_buffer_object};

  VkDeviceMemory *memory =
      array_get(&model->uniform_buffers_memory, image_index);

  pe_vk_memory_copy(sizeof(buffers), memory, buffers);
}


