#include "swordfish.h"
#include "engine/array.h"
#include <engine/model.h>

#include "renderer/pipeline.h"
#include <engine/engine2d.h>
#include <stdio.h>

#include "renderer/cglm/cglm.h"

#include "renderer/uniform_buffer.h"

PModel main_cube;
PModel secondary_cube;

PModel quad_model;

Camera main_camera;

bool finished_build = false;


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

  pe_vk_model_load(&main_cube, "/usr/libexec/swordfish/models/wireframe_cube.glb");
  pe_vk_model_load(&secondary_cube, "/usr/libexec/swordfish/models/secondary_cube.glb");


  pe_vk_create_shader(
      &main_cube.pipeline,
      "/usr/libexec/swordfish/shaders/model_view_projection_vert.spv",
      "/usr/libexec/swordfish/shaders/red_frag.spv");


  pe_vk_create_shader(&quad_model.pipeline,
                      "/usr/libexec/swordfish/shaders/dimention_2d_vert.spv",
                      "/usr/libexec/swordfish/shaders/red_frag.spv");

  secondary_cube.pipeline = main_cube.pipeline;


  pe_2d_init();
  pe_2d_create_quad(&quad_model,1,1,1,1);

}
