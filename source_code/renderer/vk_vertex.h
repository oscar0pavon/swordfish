#include "vulkan.h"
#include <engine/array.h>

typedef struct PVertexAtrributes {
  bool has_attributes;
  bool position;
  bool color;
  bool normal;
  bool uv;
  bool weight;
  bool joint;
  Array attributes_descriptions;
} PVertexAtrributes;

VkVertexInputBindingDescription pe_vk_vertex_get_binding_description();
void pe_vk_vertex_get_attribute(PVertexAtrributes* attributes);



extern VkBuffer vertex_buffer;
extern VkBuffer index_buffer;


