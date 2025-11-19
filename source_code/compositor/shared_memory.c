#include "shared_memory.h"
#include "compositor.h"
#include "engine/array.h"
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>

typedef struct SharedMemory{
  WResource *resource;
  void *data;
  uint32_t size;
}SharedMemory;

typedef struct MemoryBuffer{
    WResource*resource;
    SharedMemory *pool;
    void *data; // Pointer to the start of pixel data for this specific buffer
    int32_t width;
    int32_t height;
    int32_t stride;
    uint32_t format;
}MemoryBuffer;

void destroy_buffer(WClient *client, WResource *resource){
  wl_resource_destroy(resource);
}

void destroy_pool(WResource *resource){
  wl_resource_destroy(resource);
}

void destroy_buffer_function(WResource *resource){
  MemoryBuffer *buffer = wl_resource_get_user_data(resource);
  if(buffer){
    free(buffer);
  }
}

const struct wl_buffer_interface buffer_interface = {
  .destroy = destroy_buffer
};


const struct wl_shm_pool_interface pool_interface;

void destroy_pool_function(WClient * client, WResource *resource){
  SharedMemory* pool = wl_resource_get_user_data(resource);
  if(pool){
    munmap(pool->data, pool->size);
    free(pool);
  }
}

void create_shared_memory_pool(WClient *client, WResource *resource,
                     uint32_t id, int32_t fd, int32_t size) {


  if (size <= 0) {
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
                           "invalid pool size");
    close(fd);
    return;
  }

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  if (data == MAP_FAILED) {
    fprintf(stderr, "Failed to mmap shared memory: %m\n");
    // Post error to the client if mmap fails
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
                           "mmap failed");
    return;
  }

  WResource *pool_resource = wl_resource_create(
      client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);

  if (!pool_resource) {
    munmap(data, size);
    wl_client_post_no_memory(client);
    return;
  }

  SharedMemory *pool = calloc(1, sizeof(SharedMemory));
  if (!pool) {
    munmap(data, size);
    wl_client_post_no_memory(client);
    return;
  }

  pool->resource = pool_resource;
  pool->data = data;
  pool->size = size;
  wl_resource_set_user_data(pool_resource, pool);

  wl_resource_set_implementation(pool_resource, &pool_interface, 
      pool, 
      NULL);

  printf("Created Memory pool\n");
}

void create_shared_memory_buffer(WClient *client, 
    WResource *resource, 
    uint32_t id, int32_t offset, int32_t width, 
    int32_t height, int32_t stride, uint32_t format){

  SharedMemory *pool = wl_resource_get_user_data(resource);

  void *buffer_data = (uint8_t *)pool->data + offset;

  WResource *buffer_resource = wl_resource_create(
      client, &wl_buffer_interface, wl_resource_get_version(resource), id);


  MemoryBuffer *my_buffer_info = calloc(1, sizeof(MemoryBuffer));
  if (!my_buffer_info) {
    wl_resource_destroy(buffer_resource); // Clean up the resource first
    wl_client_post_no_memory(client);
    return;
  }

  my_buffer_info->resource = buffer_resource;
  my_buffer_info->pool = pool;
  my_buffer_info->data = buffer_data;
  my_buffer_info->width = width;
  my_buffer_info->height = height;
  my_buffer_info->stride = stride;
  my_buffer_info->format = format;

  wl_resource_set_implementation(buffer_resource, &buffer_interface,
                                 my_buffer_info, destroy_buffer_function);

  printf("Created buffer\n");

}
const struct wl_shm_pool_interface pool_interface = {
  .create_buffer = create_shared_memory_buffer,
  .destroy = destroy_pool_function
};




const struct wl_shm_interface shared_memory_implementation = {
    .create_pool = create_shared_memory_pool,
};

void bind_shared_memory(WClient *client, void *data, uint32_t version,
              uint32_t id) {

  WResource *resource =
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
