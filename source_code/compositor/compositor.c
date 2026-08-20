//PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define _GNU_SOURCE

#include "compositor.h"

#include <pthread.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>
#include <fcntl.h>
#include <errno.h>
#include <wayland-util.h>
#include "compositor/desktop-server.h"
#include "compositor/desktop.h"
#include "compositor/data_device.h"
#include "compositor/input.h"
#include "compositor/output.h"
#include "compositor/seat.h"
#include "surface.h"
#include "dma.h"
#include "shared_memory.h"
#include "swordfish.h"
#include "window.h"

//how long the compositor thread waits for a client before looking at
//swordfish_running again
#define COMPOSITOR_POLL_TIMEOUT_MS 200

SwordfishCompositor compositor;

//libwayland-server has no locking of its own and three threads send events to
//clients: this one out of the event loop, the render thread in end_frame() and
//draw_surfaces(), and the input thread in send_wayland_key() and the pointer.
//everything a client is sent goes through one ring buffer per connection whose
//head a send advances and whose tail a flush moves, so two threads at once lose
//an update and leave the tail past the head - that is the huge number in "Data
//too big for buffer (18446744073709117440 + 8 > 4096)", which is -434176 - and
//from then on the client reads a message length out of the middle of an event,
//prints "Message length 19200 exceeds limit 4096" and drops the connection.
//nothing is wrong with the events themselves, which is why it looked like the
//pointer closing the client: the pointer only made it likely, by sending
//motion at the rate the mouse moves
//
//this mutex is the whole of the serialisation. the compositor thread holds it
//across the dispatch, so it covers not just what the request handlers send but
//what libwayland sends behind them - wl_display.delete_id after every
//wl_resource_destroy, protocol errors - which no lock of ours could reach
//otherwise
//
//it is recursive because a handler cannot know whether it was reached from the
//dispatch or from the render thread, and it is the OUTERMOST lock: take it
//before draw_tasks_mutex and focus_task_mutex, never the other way round
static pthread_mutex_t wayland_mutex =
    PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

//every event a client can correlate needs its own serial. a constant made all
//of them look like the same event, which is why clients ignored the keys
uint32_t next_serial(void){
  return wl_display_next_serial(compositor.display);
}

void lock_wayland(void) {
  pthread_mutex_lock(&wayland_mutex);
}

void unlock_wayland(void) {
  pthread_mutex_unlock(&wayland_mutex);
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


void* run_compositor(void* none) {


  // Create the Wayland display
  compositor.display = wl_display_create();
  if (!compositor.display) {
    fprintf(stderr, "Failed to create Wayland display\n");
    pthread_exit(NULL);
  }

  // Get the event loop
  compositor.event_loop = wl_display_get_event_loop(compositor.display);
  if (!compositor.event_loop) {
    fprintf(stderr, "Failed to get event loop\n");
    pthread_exit(NULL);
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
    pthread_exit(NULL);
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



  //wl_display_run() is this loop with the waiting and the dispatching welded
  //together inside libwayland, which leaves nowhere to hold wayland_mutex: the
  //compositor thread would either sleep holding it, and no other thread could
  //ever send anything, or never hold it at all. unrolled, the wait happens
  //without the lock and only the dispatch takes it
  while (swordfish_running) {

    lock_wayland();
    wl_display_flush_clients(compositor.display);
    unlock_wayland();

    struct pollfd event_loop_fd = {
        .fd = wl_event_loop_get_fd(compositor.event_loop),
        .events = POLLIN,
    };

    //a timeout rather than an infinite wait, so a quiet client does not keep
    //the thread from noticing that swordfish is closing
    if (poll(&event_loop_fd, 1, COMPOSITOR_POLL_TIMEOUT_MS) < 0 &&
        errno != EINTR) {
      fprintf(stderr, "Compositor event loop poll failed: %m\n");
      break;
    }

    lock_wayland();

    //zero timeout: whatever is already there, since the poll above is what
    //waited for it
    wl_event_loop_dispatch(compositor.event_loop, 0);

    unlock_wayland();
  }

  return NULL;
}
