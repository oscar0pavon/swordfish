#include "swordfish.h"
#include "engine/array.h"
#include <engine/model.h>

#include "renderer/pipeline.h"
#include <engine/engine2d.h>
#include <stdio.h>

PModel main_cube;

PModel quad_model;

Camera main_camera;

bool finished_build = false;

VkPipeline main_cube_pipeline;
VkPipeline text_pipeline;

void swordfish_init(){

  pe_vk_model_load(&main_cube, "/usr/libexec/swordfish/models/wireframe_cube.glb");


  pe_vk_create_shader(
      &main_cube_pipeline,
      "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      "/usr/libexec/swordfish/shaders/red_frag.spv");


  pe_vk_create_shader(&text_pipeline,
                      "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
                      "/usr/libexec/swordfish/shaders/red_frag.spv");



  pe_2d_init();
  pe_2d_create_quad(&quad_model,50,50,50,50);

}
