#include "engine2d.h"
#include "engine/model.h"
#include "engine/vertex.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "../renderer/vk_buffer.h"
#include "../window.h"
#include "renderer/uniform_buffer.h"
#include "renderer/descriptor_set.h"

#include <engine/utils.h>


mat4 orthogonal_projection;

void pe_2d_init(){
  

  glm_ortho(0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0, 1, orthogonal_projection);

  
}

void pe_2d_create_quad(PModel* model, float x, float y, float width, float height){

  PVertex top_left = {.position = {x, y}};
  PVertex top_right = {.position = {x + width, y}};
  PVertex bottom_right = {.position = {x + width, y + height}};
  PVertex bottom_left = {.position = {x, y + height}};
  
  PVertex vert1;
  init_vec3(-1.0F, 1.0, 0.0, vert1.position);
  vert1.uv[0] = 0;
  vert1.uv[1] = 1;

  PVertex vert2;
  init_vec3(-1.0F, -1.0F, 0.0, vert2.position);
  vert2.uv[0] = 0;
  vert2.uv[1] = 0;

  PVertex vert3;
  init_vec3(1.0, 1.0, 0.0, vert3.position);
  vert3.uv[0] = 1;
  vert3.uv[1] = 1;

  PVertex vert4;
  init_vec3(1.0, -1.0F, 0.0, vert4.position);
  vert4.uv[0] = 1;
  vert4.uv[1] = 0;

  array_init(&model->vertex_array,sizeof(PVertex),  4);//we have 4 vertices
  array_add(&model->vertex_array, &vert1);
  array_add(&model->vertex_array, &vert2);
  array_add(&model->vertex_array, &vert3);
  array_add(&model->vertex_array, &vert4);

  //TODO maybe the size of the uint32_t can be different
  array_init(&model->index_array, sizeof(uint16_t), 6);//we use 6 index
  uint16_t number = 0;
  array_add(&model->index_array,&number);
  number = 1;
  array_add(&model->index_array,&number);
  number = 2;
  array_add(&model->index_array,&number);
  number = 0;
  array_add(&model->index_array,&number);
  number = 2;
  array_add(&model->index_array,&number);
  number = 3;
  array_add(&model->index_array,&number);


  model->vertex_buffer = pe_vk_create_buffer(model->vertex_array.bytes_size,
                                             model->vertex_array.data,
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  model->index_buffer = pe_vk_create_buffer(model->index_array.bytes_size,
                                            model->index_array.data,
                                            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  //init model matrix
  //glm_mat4_identity(model->model_mat);
  //setup Uniform Buffer Object
  //glm_mat4_copy(model->model_mat,model->uniform_buffer_object.model);
  glm_mat4_copy(orthogonal_projection, model->uniform_buffer_object.projection);

  //glm_mat4_copy(main_camera.view, model->uniform_buffer_object.view);
  //glm_mat4_copy(main_camera.projection, model->uniform_buffer_object.projection);

  pe_vk_create_uniform_buffers(model);
  pe_vk_descriptor_pool_create(model);
  pe_vk_create_descriptor_sets(model, pe_vk_descriptor_set_layout);
  pe_vk_descriptor_update(model);

}
