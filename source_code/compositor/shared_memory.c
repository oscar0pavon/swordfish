#include "shared_memory.h"
#include "compositor.h"
#include <stdio.h>



void shm_create_pool(struct wl_client *client, struct wl_resource *resource,
                     uint32_t id, int32_t fd, int32_t size) {

  printf("TODO create shared memory\n");

}

const struct wl_shm_interface shm_interface = {
    .create_pool = shm_create_pool,
};

void shm_bind(WaylandClient *client, void *data, uint32_t version,
              uint32_t id) {

  WaylandResource *resource =
      wl_resource_create(client, &wl_shm_interface, version, id);

  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &shm_interface, data, NULL);

}

void init_shared_memory() {

  wl_global_create(compositor.display, &wl_shm_interface, 1, &compositor,
                   shm_bind);
}
