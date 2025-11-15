#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "compositor/egl.h"
#include "compositor/seat.h"
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
#include "compositor/compositor.h"

#include "direct_render.h"

#include "build.h"

void close_swordfish() {
  printf("Closing Swordfish\n");
  pe_vk_end();
  swordfish_running = false;
  if(is_drm_rendering){
    finish_input();
  }
  finish_compositor();
  clear_engine_memory();
}

void handle_signal(int sig_num) {
  close_swordfish();
}

int main(){
  signal(SIGINT, handle_signal);

  pe_vk_validation_layer_enable = false;

  bool is_wayland = true;

  if(is_wayland){
    is_drm_rendering = true;
    //init_direct_render();
    compositor.gpu_path = "/dev/dri/card0";

    init_seat();
    init_egl();
  }else{
    create_window();
  }

  pe_init_memory();
  



  camera_init(&main_camera);
  

  //camera_set_position(&main_camera, VEC3(-10,0,0));
  init_vec3(-10, 0, 4, main_camera.position);
  pe_camera_look_at(&main_camera, VEC3(0,0,0));

  pe_vk_init();
  
  if(is_drm_rendering){
    // getchar();
    // clean_drm(); 
    // return EXIT_SUCCESS;
  }


  swordfish_init();

  pthread_t input_thread_id;
  pthread_create(&input_thread_id, NULL, handle_input, NULL);

  pthread_t compositor_thread_id;
  pthread_create(&compositor_thread_id,NULL,run_compositor,NULL);

  pthread_t make_thread_id;
  //pthread_create(&make_thread_id,NULL,call_make, NULL);



  start_delta_time();
  //INFO main loop
  while (swordfish_running) {
    if(compositor.gpu_fd < 0)
      continue;

    //if(is_drm_rendering)
      //check_libseat();
    //start_render_time();
    

    pe_vk_draw_frame();

    usleep(16667);//16.6ms
    update_delta_time();
    
    //delay_render_time();
  }


  if(!is_drm_rendering)
    close_window();  

  close_swordfish();

  printf("Goobye from Swordfish\n");




  return EXIT_SUCCESS;
}
