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

SwordfishCompositor compositor;


void finish_compositor(){
  
  wl_display_destroy(compositor.display);

  printf("Finish compositor\n");
}

void shm_create_pool(struct wl_client *client, struct wl_resource *resource,
                     uint32_t id, int32_t fd, int32_t size) {
  // Wayland interface implementation for wl_shm
  // A real implementation would manage shared memory pools
}

static const struct wl_shm_interface shm_interface = {
    .create_pool = shm_create_pool,
};

static void shm_bind(struct wl_client *client, void *data, uint32_t version,
                     uint32_t id) {
  struct wl_resource *resource =
      wl_resource_create(client, &wl_shm_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &shm_interface, data, NULL);
}

// Wayland interface implementation for wl_shell (simple desktop shell)
static void shell_get_shell_surface(struct wl_client *client,
                                    struct wl_resource *resource, uint32_t id,
                                    struct wl_resource *surface_resource) {
  // TODO create a shell surface object
}

static const struct wl_shell_interface shell_interface = {
    .get_shell_surface = shell_get_shell_surface,
};

static void compositor_bind(struct wl_client *client, void *data,
                            uint32_t version, uint32_t id) {

  printf("Compositor bound\n");
}

static void shell_bind(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id) {
  struct wl_resource *resource =
      wl_resource_create(client, &wl_shell_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &shell_interface, data, NULL);
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

  // Create the global registry.
  wl_global_create(compositor.display, &wl_shm_interface, 1, &compositor,
                   shm_bind);

  wl_global_create(compositor.display, &wl_shell_interface, 1, &compositor,
                   shell_bind);

  wl_global_create(compositor.display, &wl_compositor_interface, 1, &compositor,
                   compositor_bind);

  // TODO Set up rendering, input

  // Start the compositor
  const char *socket = wl_display_add_socket_auto(compositor.display);
  if (!socket) {
    fprintf(stderr, "Failed to create Wayland socket\n");
    wl_display_destroy(compositor.display);
    pthread_exit(NULL);
  }

  printf("Wayland socket available at %s\n", socket);
  printf("Compositor running. Use a Wayland client to connect.\n");

  // // This is how clients discover Wayland interfaces.
  // if (!wl_display_init_shm(compositor.display)) {
  //     fprintf(stderr, "Failed to initialize SHM\n");
  //     return 1;
  // }

  // Run the event loop indefinitely

  wl_display_run(compositor.display);
 
  
  return NULL;
}
