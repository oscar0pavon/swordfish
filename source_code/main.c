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

#include "draw.h"


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

  sword_init();

  init_compositor();

  run_compositor();

  close_sword();

  log_info("Goobye from Sword");

  log_end();




  return EXIT_SUCCESS;
}
