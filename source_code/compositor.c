#include "compositor.h"

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <engine/time.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <fcntl.h>
#include <errno.h>
#include <wayland-util.h>
#include "desktop-server.h"
#include "desktop.h"
#include "data_device.h"
#include "primary_selection.h"
#include "input.h"
#include "output.h"
#include "region.h"
#include "tty.h"
#include "surface.h"
#include "dma.h"
#include "shared_memory.h"
#include "subcompositor.h"
#include "sword.h"
#include "device_input.h"
#include "log.h"

//how long the compositor thread waits for a client before looking at
//sword_running again. now mostly a safety net - the frame timer below
//wakes the loop every ~16.7ms on its own
#define COMPOSITOR_POLL_TIMEOUT_MS 200

//how often the render loop is stepped, folded into this thread's poll set
//via a timerfd instead of main()'s own usleep(16667)
#define FRAME_INTERVAL_NS 16667000L

SwordCompositor compositor;

bool sword_running = true;

//every event a client can correlate needs its own serial. a constant made all
//of them look like the same event, which is why clients ignored the keys
uint32_t next_serial(void){
  return wl_display_next_serial(compositor.display);
}

bool is_focus_completed = true;

const WaylanCompositorInterface compositor_interface = {
  .create_surface = create_surface,
  //a region is a set of rectangles sword has nothing to do with, and this
  //entry was NULL until firefox called it: libwayland dispatches a NULL
  //handler as a call, so the request that was "not implemented" was an abort
  //inside the compositor. see region.c
  .create_region = create_region
};


void finish_compositor(){
  
  wl_display_destroy(compositor.display);

  log_info("Finish compositor");
}



void bind_compositor(WClient *client, void *data, uint32_t version,
                            uint32_t id) {

  SwordCompositor* compositor = (SwordCompositor*)data;
  if(!compositor)
    log_error("Compositor is NULL");

  WResource* resource;

  resource = wl_resource_create(client, &wl_compositor_interface, version, id);
  if(!resource){
    wl_client_post_no_memory(client);
    log_error("Can't create resource");
  }

  wl_resource_set_implementation(resource, &compositor_interface, compositor, NULL);
  log_info("Compositor bound");
}

static void your_error_handler_func(void *data, const char *msg) {
    log_error("Wayland Error: %s", msg);
}

void init_compositor(void){

  compositor.display = wl_display_create();
  if (!compositor.display) {
    log_error("Failed to create Wayland display");
    return;
  }

  compositor.event_loop = wl_display_get_event_loop(compositor.display);
  if (!compositor.event_loop) {
    log_error("Failed to get event loop");
    return;
  }

  wl_list_init(&compositor.surfaces);
  wl_list_init(&compositor.tasks_input);


  wl_global_create(compositor.display, &wl_compositor_interface,
                   COMPOSITOR_VERSION, &compositor, bind_compositor);

  //version 2, which is the one the tiled toplevel states arrived in - see
  //send_top_level_configure(). the bump costs nothing: v2 adds no request to
  //any of the four xdg interfaces, so nothing new needs a handler. v3 does,
  //and going there without writing xdg_popup.reposition and the three
  //xdg_positioner requests that came with it takes the compositor down the
  //first time a client repositions a menu
  wl_global_create(compositor.display, &xdg_wm_base_interface, 2, &compositor,
                   bind_desktop);

  //a window made of more than one surface. firefox aborts outright without
  //this global rather than degrading, so it is not optional for a gtk client
  init_subcompositor();

  init_shared_memory();

  init_dma();

  init_compositor_input();

  init_output();

  //GDK will not build a seat until this global exists, so a GTK client hangs
  //in its registry roundtrip with no keyboard and no pointer without it
  init_data_device();

  //degrades gracefully - a client that wants it and finds it missing just
  //never offers a primary selection, see primary_selection.c
  init_primary_selection();


  const char *socket = wl_display_add_socket_auto(compositor.display);
  if (!socket) {
    log_error("Failed to create Wayland socket");
    wl_display_destroy(compositor.display);
    return;
  }

  setenv("WAYLAND_DISPLAY", socket, true);
  //setenv("EGL_PLATFORM", "wayland", true);
  //setenv("EGL_LOG_LEVEL", "debug", true);
 // setenv("MESA_DEBUG", "1", true);
  //setenv("LIBGL_DEBUG", "verbose", true);
  //setenv("LIBGL_ALWAYS_SOFTWARE", "1", true);
  //setenv("EGL_WL_DRM", "1", true);
  //setenv("MESA_LOADER_DRIVER_OVERRIDE", "radeonsi", true);
  //setenv("MESA_DRM_DRIVER", "radeon", true);

  log_info("Wayland socket available at %s", socket);
  log_info("Compositor running. Use a Wayland client to connect.");

  init_input();

}

void run_compositor(void) {


  //the render loop's old usleep(16667) becomes a periodic timerfd in the same
  //poll set, so drawing a frame is just another thing this loop wakes up for
  int frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  struct itimerspec frame_interval = {
      .it_interval = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
      .it_value = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
  };
  timerfd_settime(frame_timer_fd, 0, &frame_interval, NULL);

  start_delta_time();

  while (sword_running) {

    wl_display_flush_clients(compositor.display);

    struct pollfd fds[3] = {
        {.fd = wl_event_loop_get_fd(compositor.event_loop), .events = POLLIN},
        {.fd = frame_timer_fd, .events = POLLIN},
        {.fd = libinput_get_fd(libinput), .events = POLLIN},
    };

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
}
