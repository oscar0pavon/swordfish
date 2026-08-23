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
#include "input.h"
#include "output.h"
#include "seat.h"
#include "surface.h"
#include "dma.h"
#include "shared_memory.h"
#include "swordfish.h"
#include "wayland_window/window.h"
#include "device_input.h"
#include <pway/pway.h>

//how long the compositor thread waits for a client before looking at
//swordfish_running again. now mostly a safety net - the frame timer below
//wakes the loop every ~16.7ms on its own
#define COMPOSITOR_POLL_TIMEOUT_MS 200

//how often the render loop is stepped, folded into this thread's poll set
//via a timerfd instead of main()'s own usleep(16667)
#define FRAME_INTERVAL_NS 16667000L

SwordfishCompositor compositor;

//every event a client can correlate needs its own serial. a constant made all
//of them look like the same event, which is why clients ignored the keys
uint32_t next_serial(void){
  return wl_display_next_serial(compositor.display);
}

bool is_focus_completed = true;

const WaylanCompositorInterface compositor_interface = {
  .create_surface = create_surface,
  .create_region = NULL
};


void finish_compositor(){
  
  wl_display_destroy(compositor.display);

  printf("Finish compositor\n");
}



void bind_compositor(WClient *client, void *data, uint32_t version,
                            uint32_t id) {

  SwordfishCompositor* compositor = (SwordfishCompositor*)data;
  if(!compositor)
    printf("Compositor is NULL\n");

  WResource* resource;

  resource = wl_resource_create(client, &wl_compositor_interface, version, id);
  if(!resource){
    wl_client_post_no_memory(client);
    printf("Can't create resource\n");
  }

  wl_resource_set_implementation(resource, &compositor_interface, compositor, NULL);
  printf("Compositor bound\n");
}

static void your_error_handler_func(void *data, const char *msg) {
    fprintf(stderr, "Wayland Error: %s\n", msg);
}


void run_compositor(void) {


  // Create the Wayland display
  compositor.display = wl_display_create();
  if (!compositor.display) {
    fprintf(stderr, "Failed to create Wayland display\n");
    return;
  }

  // Get the event loop
  compositor.event_loop = wl_display_get_event_loop(compositor.display);
  if (!compositor.event_loop) {
    fprintf(stderr, "Failed to get event loop\n");
    return;
  }

  wl_list_init(&compositor.surfaces);
  wl_list_init(&compositor.tasks_input);



  //advertising version 1 here is what disconnected every client: pway binds
  //wl_compositor at 4, and a bind above the advertised version is a protocol
  //error, so the client died on the registry before it ever made a surface.
  //everything version 4 adds is on wl_surface, see surface_implementation
  wl_global_create(compositor.display, &wl_compositor_interface,
                   COMPOSITOR_VERSION, &compositor, bind_compositor);

  wl_global_create(compositor.display, &xdg_wm_base_interface, 1, &compositor,
                   bind_desktop);
  
  init_shared_memory();

  init_dma();

  init_compositor_input();

  init_output();

  //GDK will not build a seat until this global exists, so a GTK client hangs
  //in its registry roundtrip with no keyboard and no pointer without it
  init_data_device();


  const char *socket = wl_display_add_socket_auto(compositor.display);
  if (!socket) {
    fprintf(stderr, "Failed to create Wayland socket\n");
    wl_display_destroy(compositor.display);
    return;
  }

  setenv("WAYLAND_DISPLAY", socket, true);
  //setenv("EGL_PLATFORM", "wayland", true);
  setenv("EGL_LOG_LEVEL", "debug", true);
 // setenv("MESA_DEBUG", "1", true);
  //setenv("LIBGL_DEBUG", "verbose", true);
  //setenv("LIBGL_ALWAYS_SOFTWARE", "1", true);
  //setenv("EGL_WL_DRM", "1", true);
  //setenv("MESA_LOADER_DRIVER_OVERRIDE", "radeonsi", true);
  //setenv("MESA_DRM_DRIVER", "radeon", true);

  printf("Wayland socket available at %s\n", socket);
  printf("Compositor running. Use a Wayland client to connect.\n");

  //this thread also pumps whichever input source we have and steps the
  //render loop, instead of a separate input thread and a separate render
  //loop on main(). is_wayland_window means the host compositor hands us
  //input through pway; otherwise libinput reads the devices directly, and
  //needs opening once before its fd exists
  if (!is_wayland_window)
    init_input();

  //the render loop's old usleep(16667) becomes a periodic timerfd in the same
  //poll set, so drawing a frame is just another thing this loop wakes up for
  int frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  struct itimerspec frame_interval = {
      .it_interval = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
      .it_value = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
  };
  timerfd_settime(frame_timer_fd, 0, &frame_interval, NULL);

  start_delta_time();

  //wl_display_run() is this loop with the waiting and the dispatching welded
  //together inside libwayland - unrolled here so pway/libinput and the frame
  //timer can be waited on in the same poll(). now that this is the only
  //thread touching any of it, nothing here needs a lock any more
  while (swordfish_running) {

    wl_display_flush_clients(compositor.display);

    //pway's own wl_display_prepare_read()/poll()/read_events() dance requires
    //prepare_read to immediately precede the poll that waits on its fd - see
    //pway_dispatch_events() below, which is the other half of this
    if (is_wayland_window)
      pway_prepare_to_read_events();

    struct pollfd fds[5];
    nfds_t nfds = 2;

    fds[0] = (struct pollfd){
        .fd = wl_event_loop_get_fd(compositor.event_loop),
        .events = POLLIN,
    };
    fds[1] = (struct pollfd){
        .fd = frame_timer_fd,
        .events = POLLIN,
    };

    if (is_wayland_window) {
      //pway->fds[2] (app_fd) is unused by swordfish and stays fd -1, which
      //poll() ignores - included anyway so pway->fds keeps lining up 1:1 with
      //fds[2..4] for the revents copy-back below
      fds[2] = pway->fds[0]; //host wayland connection
      fds[3] = pway->fds[1]; //key repeat timerfd
      fds[4] = pway->fds[3]; //paste event
      nfds = 5;
    } else if (is_drm_rendering) {
      fds[2] = (struct pollfd){
          .fd = libinput_get_fd(libinput),
          .events = POLLIN,
      };
      nfds = 3;
    }

    //a timeout rather than an infinite wait, so a quiet client does not keep
    //the thread from noticing that swordfish is closing - the frame timer
    //already wakes it every ~16.7ms in practice
    if (poll(fds, nfds, COMPOSITOR_POLL_TIMEOUT_MS) < 0 &&
        errno != EINTR) {
      fprintf(stderr, "Compositor event loop poll failed: %m\n");
      break;
    }

    //zero timeout: whatever is already there, since the poll above is what
    //waited for it
    wl_event_loop_dispatch(compositor.event_loop, 0);

    //input first, and the frame only after it. pway_prepare_to_read_events()
    //above left its connection to the host compositor in a pending read, and
    //nothing may touch that connection until this closes it - rendering does,
    //because presenting goes out through VK_KHR_wayland_surface on the very
    //same wl_display. drawing before this ran wedged the connection and no
    //input was ever read
    if (is_wayland_window) {
      pway->fds[0].revents = fds[2].revents;
      pway->fds[1].revents = fds[3].revents;
      pway->fds[3].revents = fds[4].revents;
      pway_dispatch_events();
    } else if (is_drm_rendering && (fds[2].revents & POLLIN)) {
      dispatch_libinput_events();
    }

    if (fds[1].revents & POLLIN) {
      //must be read to re-arm a periodic timerfd's readability - otherwise
      //poll() would report it ready forever after the first expiry
      uint64_t expirations;
      read(frame_timer_fd, &expirations, sizeof(expirations));
      swordfish_frame_step();
    }
  }

  close(frame_timer_fd);
}
