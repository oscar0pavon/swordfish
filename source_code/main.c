#include <EGL/egl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
#include <engine/time.h>

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

//how long the compositor thread waits for a client before looking at
//sword_running again. now mostly a safety net - the frame timer below
//wakes the loop every ~16.7ms on its own
#define COMPOSITOR_POLL_TIMEOUT_MS 200

//how often the render loop is stepped, folded into this thread's poll set
//via a timerfd instead of an old usleep(16667)
#define FRAME_INTERVAL_NS 16667000L

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
  
  log_info("Goobye from Sword");

  log_end();
}

//the teardown runs from main(); SIG_DFL back so a second ctrl+c kills a
//wedged loop
void handle_signal(int sig_num) {
  signal(sig_num, SIG_DFL);
  sword_running = false;
}


int main(void){

  sword_init();

  //the render loop's old usleep(16667) becomes a periodic timerfd in the same
  //poll set, so drawing a frame is just another thing this loop wakes up for
  int frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  struct itimerspec frame_interval = {
      .it_interval = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
      .it_value = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
  };
  timerfd_settime(frame_timer_fd, 0, &frame_interval, NULL);

  start_delta_time();

  struct pollfd fds[3] = {
      {.fd = wl_event_loop_get_fd(compositor.event_loop), .events = POLLIN},
      {.fd = frame_timer_fd, .events = POLLIN},
      {.fd = libinput_get_fd(libinput), .events = POLLIN},
  };

  while (sword_running) {

    wl_display_flush_clients(compositor.display);

    //a timeout rather than an infinite wait, so a quiet client does not keep
    //the thread from noticing that sword is closing - the frame timer
    //already wakes it every ~16.7ms in practice
    if (poll(fds, 3, COMPOSITOR_POLL_TIMEOUT_MS) < 0 &&
        errno != EINTR) {
      log_error("Compositor event loop poll failed: %m");
      break;
    }

    //zero timeout: whatever is already there, since the poll above is what
    //waited for it
    wl_event_loop_dispatch(compositor.event_loop, 0);

    if (fds[2].revents & POLLIN)
      dispatch_libinput_events();

    //a VT switch the signal handler recorded. after input, so a keypress that
    //asked for the switch is dispatched before it happens, and before the
    //frame step, which must not run once the display is gone
    tty_session_handle_pending();

    if (fds[1].revents & POLLIN) {
      //must be read to re-arm a periodic timerfd's readability - otherwise
      //poll() would report it ready forever after the first expiry
      uint64_t expirations;
      read(frame_timer_fd, &expirations, sizeof(expirations));

      //another VT owns the screen: we dropped DRM master, so presenting would
      //be submitting to a display that is not ours. clients simply do not get
      //frame callbacks until it comes back, which is what stops them drawing
      if (tty_session_is_active())
        sword_frame_step();
    }
  }

  close(frame_timer_fd);

  close_sword();

  return EXIT_SUCCESS;
}
