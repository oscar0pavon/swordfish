#include "engine2d.h"
#include "engine/model.h"
#include "engine/vertex.h"
#include <stdint.h>
#include <vulkan/vulkan_core.h>

#include "../renderer/vk_buffer.h"
#include "../window.h"
#include "renderer/cglm/clipspace/ortho_rh_no.h"
#include "renderer/uniform_buffer.h"
#include "renderer/descriptor_set.h"

#include <engine/utils.h>


mat4 orthogonal_projection;

void pe_2d_init(){
  

  glm_ortho(0, WINDOW_WIDTH, 0, WINDOW_HEIGHT, 0.1f, 1000.f, orthogonal_projection);

  
}

void pe_2d_draw(PModel* model, u32 image_index, vec2 position, vec2 size){

  mat4 model_matrix;
  glm_mat4_identity(model_matrix);

  glm_translate(model_matrix, (vec3){position[0], position[1], 0.0f});
  glm_scale(model_matrix, (vec3){size[0], size[1], 1.0f});

  glm_mat4_copy(model_matrix, model->uniform_buffer_object.model);

  pe_vk_send_uniform_buffer(model, image_index);
}

void pe_2d_create_quad_geometry(PModel* model){
  //Bottom left
  PVertex vert1;
  init_vec3(0, 0, 0.5f, vert1.position);
  vert1.uv[0] = 0;
  vert1.uv[1] = 0;

  //top left
  PVertex vert2;
  init_vec3(0, 1, 0.5f, vert2.position);
  vert2.uv[0] = 0;
  vert2.uv[1] = 1;

  //top right
  PVertex vert3;
  init_vec3(1, 1, 0.5f, vert3.position);
  vert3.uv[0] = 1;
  vert3.uv[1] = 1;

  //bottom right
  PVertex vert4;
  init_vec3(1, 0, 0.5f, vert4.position);
  vert4.uv[0] = 1;
  vert4.uv[1] = 0;

  array_init(&model->vertex_array,sizeof(PVertex),  4);//we have 4 vertices
  array_add(&model->vertex_array, &vert1);
  array_add(&model->vertex_array, &vert2);
  array_add(&model->vertex_array, &vert3);
  array_add(&model->vertex_array, &vert4);

  array_init(&model->index_array, sizeof(u16), 6);//we use 6 indices
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
  glm_mat4_identity(model->model_mat);
  //setup Uniform Buffer Object
  glm_mat4_copy(orthogonal_projection, model->uniform_buffer_object.projection);

  pe_vk_create_uniform_buffers(model);
  pe_vk_descriptor_pool_create(model);

  pe_vk_create_descriptor_sets(model, pe_vk_descriptor_set_layout_with_texture);
  pe_vk_descriptor_with_image_update(model);
}
