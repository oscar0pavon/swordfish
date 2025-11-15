#include "compositor.h"

#include <pthread.h>
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
#include "compositor/seat.h"
#include "surface.h"
#include "dma.h"
#include "shared_memory.h"
#include "swordfish.h"

SwordfishCompositor compositor;

const WaylanCompositorInterface compositor_interface = {
  .create_surface = create_surface,
  .create_region = NULL
};


void finish_compositor(){
  
  wl_display_destroy(compositor.display);

  printf("Finish compositor\n");
}



void bind_compositor(WaylandClient *client, void *data, uint32_t version,
                            uint32_t id) {

  SwordfishCompositor* compositor = (SwordfishCompositor*)data;
  if(!compositor)
    printf("Compositor is NULL\n");

  WaylandResource* resource;

  resource = wl_resource_create(client, &wl_compositor_interface, version, id);
  if(!resource){
    wl_client_post_no_memory(client);
    printf("Can't create resource\n");
  }

  wl_resource_set_implementation(resource, &compositor_interface, compositor, NULL);
  printf("Compositor bound\n");
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


  init_dma();

  init_shared_memory();

  wl_global_create(compositor.display, &xdg_wm_base_interface, 1, &compositor,
                   bind_desktop);

  wl_global_create(compositor.display, &wl_compositor_interface, 1, &compositor,
                   bind_compositor);



  const char *socket = wl_display_add_socket_auto(compositor.display);
  if (!socket) {
    fprintf(stderr, "Failed to create Wayland socket\n");
    wl_display_destroy(compositor.display);
    pthread_exit(NULL);
  }

  setenv("WAYLAND_DISPLAY", socket, true);
  setenv("EGL_PLATFORM", "wayland", true);
  setenv("EGL_LOG_LEVEL", "debug", true);
  setenv("MESA_DEBUG", "1", true);
  setenv("LIBGL_DEBUG", "verbose", true);
  setenv("LIBGL_ALWAYS_SOFTWARE", "1", true);
  //setenv("EGL_WL_DRM", "1", true);
  //setenv("MESA_LOADER_DRIVER_OVERRIDE", "radeonsi", true);
  //setenv("MESA_DRM_DRIVER", "radeon", true);

  printf("Wayland socket available at %s\n", socket);
  printf("Compositor running. Use a Wayland client to connect.\n");


  wl_display_run(compositor.display);
 
  
  return NULL;
}
