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
#include <vulkan/vulkan_core.h>

#include "renderer/cglm/cglm.h"

#include "renderer/uniform_buffer.h"

#include "renderer/descriptor_set.h"
#include "renderer/draw.h"
#include "renderer/vk_images.h"

#include <engine/time.h>

bool is_drm_rendering = false;


PModel main_cube;
PModel secondary_cube;
PModel background;

PModel quad_model;

Camera main_camera;

bool finished_build = false;

float left_position_z = -6;
bool loop_animation = false;

bool main_cube_start_scale = true;

void init_secodary_cube(PModel* model){
   
  mat4 identity;
  glm_mat4_identity(identity);
  glm_translate(identity, VEC3(0,3,-1));
  glm_translate(identity, VEC3(0,0,-3));

  glm_mat4_copy(identity, model->model_mat);
  
}

void sworfish_set_secondary_cube_position(PModel* model, uint32_t image_index){

  //this is the far the cube can reach
  float velocity = 0.0002f*delta_time;
  if(loop_animation){
    init_secodary_cube(model);
    left_position_z = -6;
    loop_animation = false;
  }
  if(left_position_z < -0.6){

    glm_translate(model->model_mat, VEC3(0,0,velocity));
    left_position_z += velocity;
  }else{
    loop_animation = true;
  }
  mat4 final_pisition;
  glm_mat4_mul(main_cube.model_mat, model->model_mat, final_pisition);

  
  //glm_mat4_copy(model->model_mat, model->uniform_buffer_object.model);
  glm_mat4_copy(final_pisition, model->uniform_buffer_object.model);

}

void draw_textured_model(PModel* model, VkCommandBuffer* cmd_buffer, u32 index){

  pe_vk_send_uniform_buffer(model, index);
  pe_vk_descriptor_with_image_update(model);

  PDrawModelCommand draw = {
    .model = model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };

  pe_vk_draw_model(&draw);
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
  draw_textured_model(&secondary_cube, cmd_buffer, index);
 
  glm_mat4_identity(background.uniform_buffer_object.model);
  draw_textured_model(&background, cmd_buffer, index);

  //quad
  pe_2d_draw(&quad_model, index, VEC2(0,0), VEC2(1,1));

  pe_vk_descriptor_update(&quad_model);

  PDrawModelCommand draw_quad = {
    .model = &quad_model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };
  pe_vk_draw_model(&draw_quad);

}


//INFO this where you update the position of the model or camera
void swordfish_update_main_cube(PModel *model, uint32_t image_index) {

  if (finished_build == false) {

    glm_rotate(model->model_mat, -0.0001f*delta_time, VEC3(0, 0, 1));
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

// INFO first load the texture
void load_textured_model(PModel* model, const char* path){
  pe_vk_model_load(model, path);
  pe_vk_create_descriptor_sets(model,pe_vk_descriptor_set_layout_with_texture);
  //pe_vk_descriptor_with_image_update(model);
}

void swordfish_init(){
 
  pe_vk_create_texture(&secondary_cube.texture, "/usr/libexec/swordfish/images/bits.png");
  pe_vk_create_texture(&quad_model.texture, "/usr/libexec/swordfish/images/font.png");
  pe_vk_create_texture(&background.texture, "/usr/libexec/swordfish/images/background1.png");

  pe_vk_model_load(&main_cube, "/usr/libexec/swordfish/models/wireframe_cube.glb");
  pe_vk_create_descriptor_sets(&main_cube,pe_vk_descriptor_set_layout);
  pe_vk_descriptor_update(&main_cube);
 
  load_textured_model(&secondary_cube,"/usr/libexec/swordfish/models/secondary_cube.glb");
  load_textured_model(&background,"/usr/libexec/swordfish/models/background.glb");


  PCreateShaderInfo main_cube_shader = {
      .transparency = false,
      .out_pipeline = &main_cube.pipeline,
      .vertex_path = "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/red_frag.spv",
      .layout = pe_vk_pipeline_layout_with_descriptors
  };


  pe_vk_create_shader(&main_cube_shader);


  PCreateShaderInfo secondary_cube_shader = {
      .transparency = false,
      .out_pipeline = &secondary_cube.pipeline,
      .vertex_path = "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  
  pe_vk_create_shader(&secondary_cube_shader);

  background.pipeline = secondary_cube.pipeline;

  init_secodary_cube(&secondary_cube);

  pe_2d_init();
  //pe_2d_create_quad_geometry(&quad_model);
  pe_2d_create_text_geometry(&quad_model,"main.o input.o window.o", 24);

  PCreateShaderInfo quad_shader = {
      .transparency = true,
      .out_pipeline = &quad_model.pipeline,
      .vertex_path = "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
      .fragment_path = "/usr/libexec/swordfish/shaders/texture_frag.spv",
      .layout = pe_vk_pipeline_layout3
  };
  pe_vk_create_shader(&quad_shader);





}
