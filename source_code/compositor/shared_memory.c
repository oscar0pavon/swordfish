#include "shared_memory.h"
#include "compositor.h"
#include "engine/array.h"
#include <stdint.h>
#include <stdio.h>

typedef struct SharedMemory{
  WaylandResource *resource;
  void *data;
  uint32_t size;
}SharedMemory;

void create_shared_memory_pool(WaylandClient *client, WaylandResource *resource,
                     uint32_t id, int32_t fd, int32_t size) {

  printf("TODO create shared memory\n");
}

const struct wl_shm_interface shared_memory_implementation = {
    .create_pool = create_shared_memory_pool,
};

void bind_shared_memory(WaylandClient *client, void *data, uint32_t version,
              uint32_t id) {

  WaylandResource *resource =
      wl_resource_create(client, &wl_shm_interface, version, id);

  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &shared_memory_implementation, data, NULL);

}

void init_shared_memory() {

  wl_display_add_shm_format(compositor.display, WL_SHM_FORMAT_ARGB8888);
  wl_display_add_shm_format(compositor.display, WL_SHM_FORMAT_XRGB8888);

  wl_global_create(compositor.display, &wl_shm_interface, 1, &compositor,
                   bind_shared_memory);
}
