#include "swordfish.h"
#include "engine/array.h"
#include <engine/model.h>

#include "renderer/cglm/affine.h"
#include "renderer/cglm/mat4.h"
#include "renderer/cglm/types.h"
#include "renderer/pipeline.h"
#include <engine/engine2d.h>
#include <stdint.h>
#include <stdio.h>

#include "renderer/cglm/cglm.h"

#include "renderer/uniform_buffer.h"

#include "renderer/descriptor_set.h"
#include "renderer/draw.h"
#include "renderer/vk_images.h"



PModel main_cube;
PModel secondary_cube;

PModel quad_model;

Camera main_camera;

bool finished_build = false;

float left_position_z = -2;

void init_secodary_cube(PModel* model){
   
  mat4 identity;
  glm_mat4_identity(identity);
  glm_translate(identity, VEC3(0,1,-1));
  glm_translate(identity, VEC3(0,0,-2));

  glm_mat4_copy(identity, model->model_mat);
  
}

void sworfish_set_secondary_cube_position(PModel* model, uint32_t image_index){

  //this is the far the cube can reach
  float velocity = 0.001f;
  if(left_position_z < -0.1){

    glm_translate(model->model_mat, VEC3(0,0,velocity));
    left_position_z += velocity;
  }
  mat4 final_pisition;
  glm_mat4_mul(main_cube.model_mat, model->model_mat, final_pisition);

  
  //glm_mat4_copy(model->model_mat, model->uniform_buffer_object.model);
  glm_mat4_copy(final_pisition, model->uniform_buffer_object.model);

  pe_vk_send_uniform_buffer(model, image_index);
}


void swordfish_draw_scene(VkCommandBuffer *cmd_buffer, uint32_t index){

  //main cube
  swordfish_update_main_cube(&main_cube, index);
  pe_vk_descriptor_update(&main_cube);

  PDrawModelCommand draw_cube = {
    .model = &main_cube,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout_with_descriptors
  };
  pe_vk_draw_model(&draw_cube);

  //secondary_cube
  sworfish_set_secondary_cube_position(&secondary_cube, index);
  pe_vk_descriptor_with_image_update(&secondary_cube);

  PDrawModelCommand draw_seconday_cube = {
    .model = &secondary_cube,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };

  pe_vk_draw_model(&draw_seconday_cube);

  //quad
  pe_2d_draw(&quad_model, index, VEC2(50,50), VEC2(100,100));

  pe_vk_descriptor_update(&quad_model);

  PDrawModelCommand draw_quad = {
    .model = &quad_model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout_with_descriptors
  };
  pe_vk_draw_model(&draw_quad);

}


//INFO this where you update the position of the model or camera
void swordfish_update_main_cube(PModel *model, uint32_t image_index) {

  if (finished_build == false) {

    glm_rotate(model->model_mat, -0.0001f, VEC3(0, 0, 1));
    glm_mat4_copy(model->model_mat, model->uniform_buffer_object.model);
  } else {
    mat4 identity;
    glm_mat4_identity(identity);
    float scale_value = 0.6f;
    vec3 scale = {scale_value, scale_value, scale_value};
    glm_scale(identity, scale);
    glm_mul(model->model_mat, identity, model->uniform_buffer_object.model);
  }

  // pawn_ubo.projection[1][1] *= -1; //TODO maybe we need to do this

  pe_vk_send_uniform_buffer(model, image_index);

}

void swordfish_init(){
 
  pe_vk_create_texture(&secondary_cube.texture, "/usr/libexec/swordfish/images/bits.png");


  pe_vk_model_load(&main_cube, "/usr/libexec/swordfish/models/wireframe_cube.glb");
  pe_vk_create_descriptor_sets(&main_cube,pe_vk_descriptor_set_layout);
  pe_vk_descriptor_update(&main_cube);
  
  pe_vk_model_load(&secondary_cube, "/usr/libexec/swordfish/models/secondary_cube.glb");
  pe_vk_create_descriptor_sets(&secondary_cube,pe_vk_descriptor_set_layout_with_texture);
  pe_vk_descriptor_with_image_update(&secondary_cube);

  pe_vk_create_shader(
      &main_cube.pipeline,
      "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      "/usr/libexec/swordfish/shaders/red_frag.spv",
      pe_vk_pipeline_layout_with_descriptors);


  
  pe_vk_create_shader(
      &secondary_cube.pipeline,
      "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      "/usr/libexec/swordfish/shaders/texture_frag.spv",
      pe_vk_pipeline_layout3);

  init_secodary_cube(&secondary_cube);

  pe_2d_init();
  pe_2d_create_quad_geometry(&quad_model);

  pe_vk_create_shader(&quad_model.pipeline,
                      "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
                      "/usr/libexec/swordfish/shaders/red_frag.spv",
                      pe_vk_pipeline_layout_with_descriptors);
}
