#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>


#include "engine/camera.h"
#include "engine/model.h"
#include "input.h"
#include "renderer/vulkan.h"
#include "window.h"

#include <engine/memory.h>

#include "renderer/draw.h"

#include "swordfish.h"

#include "renderer/uniform_buffer.h"
#include "renderer/descriptor_set.h"

#include <engine/utils.h>

#include <engine/time.h>

#include "build.h"

PModel main_cube;

Camera main_camera;

bool finished_build = false;

int main(){

  pe_vk_validation_layer_enable = false;

  pe_init_memory();
  

  create_window();

  camera_init(&main_camera);
  
  init_vec3(-10, 0, 0, main_camera.position);

  camera_update(&main_camera);

  pe_vk_init();



  pe_vk_model_load(&main_cube, "models/wireframe_cube.glb");

  glm_mat4_identity(main_cube.model_mat);

  pe_vk_create_uniform_buffers(&main_cube);
  pe_vk_descriptor_pool_create(&main_cube);
  pe_vk_create_descriptor_sets(&main_cube);

  pthread_t thread_id;

  pthread_create(&thread_id,NULL,handle_input, NULL);//TODO to much CPU usage


  pthread_t make_thread_id;
  pthread_create(&make_thread_id,NULL,call_make, NULL);

  while (swordfish_running) {

    start_render_time();
   //draw cube 
   // printf("Compiling..\n");
    //sleep(1);

    pe_vk_draw_frame();

    delay_render_time();
  }


  close_window();  

  clear_engine_memory();




  return EXIT_SUCCESS;
}
