
#include "vk_vertex.h"
#include "vk_buffer.h"
#include "vk_memory.h"
#include "vulkan.h"
#include <engine/array.h>
#include <engine/macros.h>
#include <engine/vertex.h>

#include <engine/model.h>

#include "descriptor_set.h"
#include "uniform_buffer.h"

VkBuffer vertex_buffer;
VkBuffer index_buffer;

VkVertexInputBindingDescription pe_vk_vertex_get_binding_description() {
  VkVertexInputBindingDescription binding = {.binding = 0,
                                             .stride = sizeof(PVertex),
                                             .inputRate =
                                                 VK_VERTEX_INPUT_RATE_VERTEX};
  return binding;
}

void pe_vk_vertex_get_attribute(PVertexAtrributes *attributes) {

  if (attributes->position) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 0;

    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = offsetof(PVertex, position);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
  if (attributes->color) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 1;

    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = offsetof(PVertex, color);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
  if (attributes->normal) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 2;

    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = offsetof(PVertex, normal);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
  if (attributes->uv) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 3;

    attribute.format = VK_FORMAT_R32G32_SFLOAT;
    attribute.offset = offsetof(PVertex, uv);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
  if (attributes->joint) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 4;

    attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute.offset = offsetof(PVertex, joint);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
  if (attributes->weight) {
    VkVertexInputAttributeDescription attribute;
    ZERO(attribute);

    attribute.binding = 0;
    attribute.location = 5;

    attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute.offset = offsetof(PVertex, weight);
    array_add(&attributes->attributes_descriptions, &attribute);
  }
}



