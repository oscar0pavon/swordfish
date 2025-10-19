#include <X11/Xlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>


#include "input.h"
#include "renderer/vulkan.h"
#include "window.h"

#include <engine/memory.h>

#include "renderer/draw.h"

int main(){

  pe_init_memory();
  

  create_window();

  
  pe_vk_init();

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
