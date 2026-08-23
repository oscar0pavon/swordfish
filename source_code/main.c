#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "tty.h"
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
#include "log.h"



void close_swordfish() {
  log_info("Closing Swordfish");

  swordfish_running = false;

  //INFO first, before any of the teardown below. giving the display back is
  //the one step that must not be skipped: leaving VT_PROCESS set with nobody
  //left to answer the release signal wedges VT switching for the whole
  //machine, and it has to be reached even when the rest of the shutdown does
  //not - pe_vk_end() below aborts inside vkDestroyDevice if a client is still
  //connected, which is exactly the state a ctrl+c arrives in
  if(is_drm_rendering){
    finish_input();
    tty_session_finish();
  }

  clean_swordfish();
  pe_vk_end();

  finish_keyboard();
  finish_compositor();
  clear_engine_memory();
}

void handle_signal(int sig_num) {
  close_swordfish();
}

int main(void){

  log_init();

  signal(SIGINT, handle_signal);
  //pkill's default. without it a kill leaves the tty in graphics mode with
  //VT_PROCESS still set, and no VT can be switched to afterwards
  signal(SIGTERM, handle_signal);

  pe_init_memory();
  
  array_init(&tasks_for_draw, sizeof(void *), 50);

  init_keyboard();

  pe_vk_validation_layer_enable = true;

  if(!create_wayland_window()){
    is_drm_rendering = true;

    //no host compositor: this is a VT, and the console the printf() calls all
    //over swordfish are writing to is about to be under the frames we draw
    log_redirect_stdio();

    compositor.gpu_path = "/dev/dri/card0";

    //INFO before pe_vk_init(), and that ordering is the whole trick: this
    //takes DRM master, which makes the fd radv opens for itself non-master,
    //which leaves mesa's wsi_display with no fd of its own - so the hook below
    //can install ours instead and swordfish can drop the display again when
    //the VT is switched away. taking master after vulkan is up is too late
    if (!tty_session_init(compositor.gpu_path))
      log_warn("No VT session: switching away will not release the display");

    pe_vk_acquire_display = swordfish_acquire_drm_display;
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


  log_info("Goobye from Swordfish");

  log_end();




  return EXIT_SUCCESS;
}
