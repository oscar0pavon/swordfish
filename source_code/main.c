#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "seat.h"
#include "surface.h"
#include <engine/array.h>
#include "swordfish.h"

#include <engine/camera.h>
#include "device_input.h"
#include "keyboard.h"
#include <engine/renderer/vulkan.h>
#include "outputs.h"
#include "wayland_window/window.h"

#include <engine/memory.h>

#include <engine/renderer/renderer.h>

#include "compositor.h"



void close_swordfish() {
  printf("Closing Swordfish\n");
    
  clean_swordfish();
  pe_vk_end();

  swordfish_running = false;
  if(is_drm_rendering){
    finish_input();
  }
  finish_keyboard();
  finish_compositor();
  clear_engine_memory();
}

void handle_signal(int sig_num) {
  close_swordfish();
}

int main(void){

  signal(SIGINT, handle_signal);

  pe_init_memory();
  
  array_init(&tasks_for_draw, sizeof(void *), 50);

  init_keyboard();

  pe_vk_validation_layer_enable = true;

  if(!create_wayland_window()){
    is_drm_rendering = true;

    compositor.gpu_path = "/dev/dri/card0";
    
    //we can use seat but with vulkan not for now
    //init_seat();
  }

  pe_window_width = WINDOW_WIDTH;
  pe_window_height = WINDOW_HEIGHT;

  pe_vk_draw_scene = swordfish_draw_scene;

  pe_vk_init();

  swordfish_outputs_init();

  camera_init(&main_camera);


  swordfish_init();

  init_compositor();

  run_compositor();

finish:
  if(is_wayland_window)
    close_wayland_window();


  printf("Goobye from Swordfish\n");




  return EXIT_SUCCESS;
}
