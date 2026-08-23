#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "input.h"
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

#include <engine/renderer/draw.h>
#include <engine/renderer/render_thread.h>
#include <engine/renderer/renderer.h>

#include <engine/utils.h>

#include <engine/time.h>
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

int main(){
  signal(SIGINT, handle_signal);

  pe_init_memory();
  
  //array_init takes the element size first and the count second, and these two
  //were the other way round: a stride of 50 bytes per Task* left room for 8
  //surfaces in a 400 byte block, and every add memcpy'd 50 bytes out of a
  //place that held 8
  array_init(&tasks_for_draw, sizeof(void *), 50);

  //the keymap is the compositor's, not the libinput path's: every client that
  //asks for a keyboard is sent it, whoever is feeding us keys. it used to be
  //initialized inside handle_input(), which the pway path returns before
  //reaching, so serving a client meant reading a NULL xkb_keymap
  init_keyboard();

  pe_vk_validation_layer_enable = true;

  //a wayland window through pway, or nothing and we drive DRM directly
  if(!create_wayland_window()){
    is_drm_rendering = true;

    compositor.gpu_path = "/dev/dri/card0";
    
    //we can use seat but with vulkan not for now
    //init_seat();
  }

  pthread_t input_thread_id;
  pthread_create(&input_thread_id, NULL, handle_input, NULL);

  //graphics stuff
  // For pengine - Vulkan Rendering

  //the renderer draws at whatever size it is told; swordfish's is fixed and
  //the window, the swap chain and the 2D projection all have to agree on it
  pe_window_width = WINDOW_WIDTH;
  pe_window_height = WINDOW_HEIGHT;

  //the scene is the application's. the renderer records the render pass and
  //calls back into this in the middle of it
  pe_vk_draw_scene = swordfish_draw_scene;

  pe_vk_init();

  //one SwordfishOutput per render target, laid left to right. static
  //detection only - has to run before the compositor thread, which is what
  //advertises these as wl_outputs and reads them into the layout
  swordfish_outputs_init();

  //INFO after pe_vk_init(), not before: under DRM it is what corrects
  //pe_window_width/height from the fixed WINDOW_WIDTH/HEIGHT above to the
  //display's real mode, and camera_init() reads them for the aspect ratio
  camera_init(&main_camera);

  //camera_set_position(&main_camera, VEC3(-10,0,0));
  //the position set here would never be seen: swordfish_update_camera() runs
  //the dolly and rewrites it every frame before anything reads the view
  //matrix. the DOLLY_ constants in swordfish.c are what frames the scene

  swordfish_init();

  //the socket goes up only now. everything a client touches on the compositor
  //thread - the quad's pipeline, the buffers behind it, the dmabuf format
  //table the GPU is asked for - needs a vulkan device, and the thread used to
  //start before pe_vk_init() ran. a client that connected in that window died
  //on "vkCreateBuffer: Invalid device" and took swordfish with it
  pthread_t compositor_thread_id;
  pthread_create(&compositor_thread_id,NULL,run_compositor,NULL);

  start_delta_time();
  //INFO main loop
  while (swordfish_running) {
    if(compositor.gpu_fd < 0)
      continue;

    handle_focus();

    //start_render_time();


    pe_frame_draw();

    usleep(16667);//16.6ms
    update_delta_time();
  
    end_frame();
    //sleep(1);
    //delay_render_time();
    //array_clean(&surface_to_draw);
  }

finish:
  if(is_wayland_window)
    close_wayland_window();


  printf("Goobye from Swordfish\n");




  return EXIT_SUCCESS;
}
