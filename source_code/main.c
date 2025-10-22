#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "swordfish.h"

#include "engine/camera.h"
#include "engine/engine2d.h"
#include "engine/model.h"
#include "input.h"
#include "renderer/vulkan.h"
#include "window.h"

#include <engine/memory.h>

#include "renderer/draw.h"



#include <engine/utils.h>

#include <engine/time.h>

#include "build.h"


int main(){

  pe_vk_validation_layer_enable = true;

  pe_init_memory();
  

  create_window();

  camera_init(&main_camera);
  

  camera_set_position(&main_camera, VEC3(-10,0,0));

  pe_vk_init();


  swordfish_init();



  pthread_t thread_id;

  pthread_create(&thread_id,NULL,handle_input, NULL);//TODO to much CPU usage


  pthread_t make_thread_id;
  pthread_create(&make_thread_id,NULL,call_make, NULL);



  //INFO main loop
  while (swordfish_running) {

    start_render_time();

    pe_vk_draw_frame();

    delay_render_time();
    
  }


  close_window();  

  clear_engine_memory();




  return EXIT_SUCCESS;
}
