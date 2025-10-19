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

PModel main_cube;

Camera main_camera;

int main(){

  pe_init_memory();
  

  create_window();

  camera_init(&main_camera);
  
  init_vec3(-10, 0, 0, main_camera.position);

  camera_update(&main_camera);

  pe_vk_init();



  pe_vk_model_load(&main_cube, "models/wireframe_cube.glb");

  pe_vk_create_uniform_buffers(&main_cube);
  pe_vk_descriptor_pool_create(&main_cube);
  pe_vk_create_descriptor_sets(&main_cube);

  pthread_t thread_id;

  pthread_create(&thread_id,NULL,handle_input, NULL);


  while (swordfish_running) {
  
   //draw cube 
   // printf("Compiling..\n");
    //sleep(1);

    pe_vk_draw_frame();

  }


  close_window();  

  clear_engine_memory();




  return EXIT_SUCCESS;
}
