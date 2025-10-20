#include "engine2d.h"
#include "engine/model.h"
#include "engine/vertex.h"
#include <vulkan/vulkan_core.h>

#include "../renderer/vk_buffer.h"


void pe_2d_create_quad(PModel* model, float x, float y, float width, float height){

  PVertex top_left = {.position = {x, y}};
  PVertex top_right = {.position = {x + width, y}};
  PVertex bottom_right = {.position = {x + width, y + height}};
  PVertex bottom_left = {.position = {x, y + height}};

  array_init(&model->vertex_array,sizeof(PVertex),  4);
  array_add(&model->vertex_array, &top_left);
  array_add(&model->vertex_array, &top_right);
  array_add(&model->vertex_array, &bottom_right);
  array_add(&model->vertex_array, &bottom_left);


  pe_vk_create_buffer(&model->vertex_array, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

}
