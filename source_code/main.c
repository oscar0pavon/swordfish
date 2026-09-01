#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "tty.h"
#include "surface.h"
#include <engine/array.h>
#include "sword.h"

#include <engine/camera.h>
#include "device_input.h"
#include "keyboard.h"
#include "launch.h"
#include <engine/renderer/vulkan.h>
#include "outputs.h"

#include <engine/memory.h>

#include <engine/renderer/renderer.h>

#include "compositor.h"
#include "log.h"



void close_sword() {

  static bool closed;
  if (closed)
    return;
  closed = true;

  log_info("Closing Sword");

  sword_running = false;

  //INFO first, before any of the teardown below. giving the display back is
  //the one step that must not be skipped: leaving VT_PROCESS set with nobody
  //left to answer the release signal wedges VT switching for the whole
  //machine, and it has to be reached even when the rest of the shutdown does
  //not - pe_vk_end() below aborts inside vkDestroyDevice if a client is still
  //connected, which is exactly the state a ctrl+c arrives in
  finish_input();
  tty_session_finish();

  //before pe_vk_end(), which aborts inside vkDestroyDevice if a client is
  //still connected: the programs sword spawned are exactly those clients, and
  //this is the only thing that ends them - they are init's children by then
  launch_close_programs();

  clean_sword();
  pe_vk_end();

  finish_keyboard();
  finish_compositor();
  clear_engine_memory();
}

//the teardown runs from main(); SIG_DFL back so a second ctrl+c kills a
//wedged loop
void handle_signal(int sig_num) {
  signal(sig_num, SIG_DFL);
  sword_running = false;
}

int main(void){

  log_init();

  signal(SIGINT, handle_signal);
  //pkill's default. without it a kill leaves the tty in graphics mode with
  //VT_PROCESS still set, and no VT can be switched to afterwards
  signal(SIGTERM, handle_signal);

  //INFO measured arena usage under normal load is ~15KB (Arrays and other
  //bookkeeping structs only - client buffers and textures go through Vulkan,
  //not this arena). 16MB leaves generous headroom without the old 750MB's
  //memset committing that much RSS on every startup
  pe_init_memory(16 * 1024 * 1024);
  
  array_init(&tasks_for_draw, sizeof(void *), 50);

  init_keyboard();

  pe_vk_validation_layer_enable = true;

  is_drm_rendering = true;

  //the console the printf() calls all over sword are writing to is about to
  //be under the frames we draw
  log_redirect_stdio();

  compositor.gpu_path = "/dev/dri/card0";

  //INFO before pe_vk_init(), and that ordering is the whole trick: this
  //takes DRM master, which makes the fd radv opens for itself non-master,
  //which leaves mesa's wsi_display with no fd of its own - so the hook below
  //can install ours instead and sword can drop the display again when
  //the VT is switched away. taking master after vulkan is up is too late
  if (!tty_session_init(compositor.gpu_path))
    log_warn("No VT session: switching away will not release the display");

  pe_vk_acquire_display = sword_acquire_drm_display;
  pe_vk_sort_displays = sword_sort_displays_by_connector;

  pe_window_width = WINDOW_WIDTH;
  pe_window_height = WINDOW_HEIGHT;

  pe_vk_draw_scene = sword_draw_scene;

  pe_vk_init();

  sword_outputs_init();

  sword_capture_display_routing();
  sword_log_display_routing("startup");

  camera_init(&main_camera);


  sword_init();

  init_compositor();

  run_compositor();

  close_sword();

  log_info("Goobye from Sword");

  log_end();




  return EXIT_SUCCESS;
}
