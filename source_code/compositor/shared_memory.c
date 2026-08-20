//mremap, which resize_shared_memory_pool() needs
#define _GNU_SOURCE

#include "shared_memory.h"
#include "compositor.h"
#include "drm_format.h"
#include <drm/drm_fourcc.h>
#include <engine/array.h>
#include <engine/renderer/vk_buffer.h>
#include <engine/renderer/vk_images.h>
#include <engine/renderer/vulkan.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-protocol.h>

//wl_shm names its two mandatory formats 0 and 1 rather than by fourcc; every
//other value in the enum is the fourcc itself. drm_format.c is the one place
//that says what a fourcc means in vulkan, so the two are translated into it
//rather than mapped a second time here
#define SHARED_MEMORY_FORMAT_ARGB8888 0
#define SHARED_MEMORY_FORMAT_XRGB8888 1

//the mapping a client's buffers are cut out of. the protocol says destroying
//the pool does not destroy the buffers made from it - the memory goes when the
//last of them does - so the mapping is reference counted rather than unmapped
//in the destroy handler
typedef struct SharedMemoryPool {
  WResource *resource;
  void *data;
  uint32_t size;
  //buffers still alive, plus one for the pool resource itself while it lives
  int reference_count;
} SharedMemoryPool;

//what an shm buffer leaves behind on the gpu. the client is free to destroy a
//buffer in the middle of a frame the render thread is recording, so none of it
//can be destroyed where the request arrives
typedef struct DeadTexture {
  PTexture texture;
  PBuffer staging;
  bool has_staging;
} DeadTexture;

#define SHARED_MEMORY_MAX_DEAD_TEXTURES 64
static DeadTexture dead_textures[SHARED_MEMORY_MAX_DEAD_TEXTURES];
static int dead_texture_count;
static pthread_mutex_t dead_textures_mutex = PTHREAD_MUTEX_INITIALIZER;

static void pool_reference(SharedMemoryPool *pool) { pool->reference_count++; }

static void pool_unreference(SharedMemoryPool *pool) {

  if (--pool->reference_count > 0)
    return;

  munmap(pool->data, pool->size);
  free(pool);
}

// ---------------------------------------------------------------- the buffer

static uint32_t shared_memory_format_to_fourcc(uint32_t format) {

  if (format == SHARED_MEMORY_FORMAT_ARGB8888)
    return DRM_FORMAT_ARGB8888;

  if (format == SHARED_MEMORY_FORMAT_XRGB8888)
    return DRM_FORMAT_XRGB8888;

  return format;
}

//the bytes the pixels occupy once the client's row padding is gone, which is
//the only layout vkCmdCopyBufferToImage is being told about
static VkDeviceSize buffer_packed_size(const ClientBuffer *buffer) {
  return (VkDeviceSize)buffer->width * buffer->height * 4;
}

//pe_vk_create_image_view() has no way to say this, and it is not optional: an X
//format leaves the fourth byte undefined, so the alpha the quad blends with is
//whatever the client's toolkit happened to leave in the buffer. the dmabuf path
//answers it the same way inside pe_vk_import_image()
static VkImageView create_buffer_image_view(const ClientBuffer *buffer) {

  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = buffer->texture.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = buffer->format,
      .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .subresourceRange.baseMipLevel = 0,
      .subresourceRange.levelCount = buffer->texture.mip_level,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount = 1};

  if (buffer->opaque)
    view_info.components.a = VK_COMPONENT_SWIZZLE_ONE;

  VkImageView view = VK_NULL_HANDLE;
  VKVALID(vkCreateImageView(vk_device, &view_info, NULL, &view),
          "Can't create image view for a shared memory buffer");

  return view;
}

//the image is made here rather than in create_buffer because creating it means
//a layout transition, and that is a queue submit - it has to happen on the
//thread that owns the queue, which is the one that calls the upload
static bool create_buffer_texture(ClientBuffer *buffer) {

  PImageCreateInfo image_info = {
      .width = buffer->width,
      .height = buffer->height,
      .texture = &buffer->texture,
      .format = buffer->format,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      .properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .number_of_samples = VK_SAMPLE_COUNT_1_BIT};

  pe_vk_create_image(&image_info);

  if (buffer->texture.image == VK_NULL_HANDLE) {
    printf("Could not create an image for a shared memory buffer\n");
    return false;
  }

  //once, and it stays there. pengine samples its own textures in
  //TRANSFER_DST_OPTIMAL as well, and leaving the image in it means every later
  //upload is a copy and nothing else
  pe_vk_transition_image_layout(buffer->texture.image, buffer->format,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                buffer->texture.mip_level);

  buffer->texture.image_view = create_buffer_image_view(buffer);

  pe_vk_create_texture_sampler(&buffer->texture);

  //pe_vk_create_buffer() copies pixels in as it allocates, and there are none
  //yet - this one is filled by every upload instead of being made by one
  PBufferCreateInfo staging_info = {
      .size = buffer_packed_size(buffer),
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};

  pe_vk_create_buffer_memory(&staging_info);

  buffer->staging.buffer = staging_info.buffer;
  buffer->staging.memory = staging_info.buffer_memory;
  buffer->has_staging = true;

  return true;
}

bool shared_memory_upload(ClientBuffer *buffer) {

  if (buffer->texture.image == VK_NULL_HANDLE && !create_buffer_texture(buffer))
    return false;

  const uint8_t *rows = (const uint8_t *)buffer->pool->data + buffer->offset;

  uint32_t packed_stride = buffer->width * 4;
  VkDeviceSize size = buffer_packed_size(buffer);

  void *staged = NULL;
  vkMapMemory(vk_device, buffer->staging.memory, 0, size, 0, &staged);

  //row by row, because the client's stride is its own business - a toolkit pads
  //to whatever suited it and only the first width * 4 bytes of each row are
  //pixels. copying straight into the mapped staging memory is what keeps this
  //to one pass over the image rather than two
  if ((uint32_t)buffer->stride == packed_stride) {
    memcpy(staged, rows, size);
  } else {
    for (int y = 0; y < buffer->height; y++)
      memcpy((uint8_t *)staged + (size_t)y * packed_stride,
             rows + (size_t)y * buffer->stride, packed_stride);
  }

  vkUnmapMemory(vk_device, buffer->staging.memory);

  pe_vk_image_copy_buffer(buffer->staging.buffer, buffer->texture.image,
                          buffer->width, buffer->height);

  buffer->needs_upload = false;

  return true;
}

//queued rather than destroyed: this runs on the compositor thread, and the
//render thread may be recording a draw that samples this image right now
static void retire_buffer_texture(ClientBuffer *buffer) {

  if (buffer->texture.image == VK_NULL_HANDLE)
    return;

  pthread_mutex_lock(&dead_textures_mutex);

  if (dead_texture_count < SHARED_MEMORY_MAX_DEAD_TEXTURES) {
    DeadTexture *dead = &dead_textures[dead_texture_count++];
    dead->texture = buffer->texture;
    dead->staging = buffer->staging;
    dead->has_staging = buffer->has_staging;
  } else {
    printf("Too many dead shared memory textures, leaking one\n");
  }

  pthread_mutex_unlock(&dead_textures_mutex);
}

void shared_memory_collect_textures(void) {

  pthread_mutex_lock(&dead_textures_mutex);

  for (int i = 0; i < dead_texture_count; i++) {
    pe_vk_clean_image(&dead_textures[i].texture);

    if (!dead_textures[i].has_staging)
      continue;

    vkDestroyBuffer(vk_device, dead_textures[i].staging.buffer, NULL);
    vkFreeMemory(vk_device, dead_textures[i].staging.memory, NULL);
  }

  dead_texture_count = 0;

  pthread_mutex_unlock(&dead_textures_mutex);
}

static void destroy_buffer(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static void destroy_buffer_function(WResource *resource) {
  ClientBuffer *buffer = wl_resource_get_user_data(resource);

  if (!buffer)
    return;

  retire_buffer_texture(buffer);
  pool_unreference(buffer->pool);

  free(buffer);
}

static const struct wl_buffer_interface buffer_interface = {
    .destroy = destroy_buffer};

static void create_shared_memory_buffer(WClient *client, WResource *resource,
                                        uint32_t id, int32_t offset,
                                        int32_t width, int32_t height,
                                        int32_t stride, uint32_t format) {

  SharedMemoryPool *pool = wl_resource_get_user_data(resource);

  const DrmFormat *drm_format =
      drm_format_find(shared_memory_format_to_fourcc(format));

  //only the two formats init_shared_memory() advertises can arrive, so this is
  //a client that ignored the list rather than one asking for something exotic
  if (!drm_format) {
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FORMAT,
                           "unsupported buffer format");
    return;
  }

  if (width <= 0 || height <= 0 || stride < width * 4 || offset < 0) {
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
                           "invalid buffer geometry");
    return;
  }

  //the last row is only stride bytes into the pool if every row before it was,
  //and a client that gets this wrong would have the compositor read past the
  //end of its mapping
  if ((int64_t)offset + (int64_t)stride * height > (int64_t)pool->size) {
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
                           "buffer does not fit in the pool");
    return;
  }

  WResource *buffer_resource = wl_resource_create(
      client, &wl_buffer_interface, wl_resource_get_version(resource), id);

  if (!buffer_resource) {
    wl_client_post_no_memory(client);
    return;
  }

  ClientBuffer *buffer = calloc(1, sizeof(ClientBuffer));
  if (!buffer) {
    wl_resource_destroy(buffer_resource);
    wl_client_post_no_memory(client);
    return;
  }

  buffer->type = CLIENT_BUFFER_SHARED_MEMORY;
  buffer->pool = pool;
  buffer->offset = offset;
  buffer->width = width;
  buffer->height = height;
  buffer->stride = stride;
  buffer->format = drm_format->vulkan;
  buffer->opaque = drm_format->opaque;
  buffer->needs_upload = true;

  //the quad reads the size off the texture, and the image behind it does not
  //exist until the first upload
  buffer->texture.width = width;
  buffer->texture.heigth = height;
  buffer->texture.mip_level = 1;

  //the mapping outlives the pool resource if buffers are still cut out of it
  pool_reference(pool);

  wl_resource_set_implementation(buffer_resource, &buffer_interface, buffer,
                                 destroy_buffer_function);

  printf("Created shared memory buffer %ix%i %s stride %i\n", width, height,
         drm_format->name, stride);
}

// ------------------------------------------------------------------ the pool

//wl_shm_pool.resize, which every toolkit sends the moment its window grows. a
//pool only ever grows, and the mapping has to grow with it or the buffers made
//afterwards are read past the end of it. leaving this NULL is dispatched as a
//call and aborts the whole compositor - libwayland says "listener function for
//opcode 2 of wl_shm_pool is NULL" and takes swordfish down with the client
static void resize_shared_memory_pool(WClient *client, WResource *resource,
                                      int32_t size) {

  SharedMemoryPool *pool = wl_resource_get_user_data(resource);
  if (!pool)
    return;

  if (size <= 0 || (uint32_t)size <= pool->size)
    return;

  void *data = mremap(pool->data, pool->size, size, MREMAP_MAYMOVE);

  if (data == MAP_FAILED) {
    fprintf(stderr, "Failed to remap shared memory: %m\n");
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "remap failed");
    return;
  }

  pool->data = data;
  pool->size = size;
}

//the request only says the client is finished with the pool object. the
//mapping stays until the last buffer cut out of it is destroyed too
static void destroy_pool(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static void destroy_pool_function(WResource *resource) {
  SharedMemoryPool *pool = wl_resource_get_user_data(resource);

  if (pool)
    pool_unreference(pool);
}

static const struct wl_shm_pool_interface pool_interface = {
    .create_buffer = create_shared_memory_buffer,
    .destroy = destroy_pool,
    .resize = resize_shared_memory_pool};

static void create_shared_memory_pool(WClient *client, WResource *resource,
                                      uint32_t id, int32_t fd, int32_t size) {

  if (size <= 0) {
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
                           "invalid pool size");
    close(fd);
    return;
  }

  void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

  //the fd is ours once it has been mapped, and the mapping keeps the memory
  //alive without it. leaking one per pool runs the client out of descriptors
  close(fd);

  if (data == MAP_FAILED) {
    fprintf(stderr, "Failed to mmap shared memory: %m\n");
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD, "mmap failed");
    return;
  }

  WResource *pool_resource = wl_resource_create(
      client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);

  if (!pool_resource) {
    munmap(data, size);
    wl_client_post_no_memory(client);
    return;
  }

  SharedMemoryPool *pool = calloc(1, sizeof(SharedMemoryPool));
  if (!pool) {
    munmap(data, size);
    wl_resource_destroy(pool_resource);
    wl_client_post_no_memory(client);
    return;
  }

  pool->resource = pool_resource;
  pool->data = data;
  pool->size = size;
  //the pool resource's own reference, dropped when the client destroys it
  pool->reference_count = 1;

  wl_resource_set_implementation(pool_resource, &pool_interface, pool,
                                 destroy_pool_function);

  printf("Created Memory pool\n");
}

// --------------------------------------------------------------------- wl_shm

static const struct wl_shm_interface shared_memory_implementation = {
    .create_pool = create_shared_memory_pool};

static void bind_shared_memory(WClient *client, void *data, uint32_t version,
                               uint32_t id) {

  WResource *resource =
      wl_resource_create(client, &wl_shm_interface, version, id);

  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &shared_memory_implementation, data,
                                 NULL);

  //the formats are sent on bind, not once at startup: wl_shm.format is an event
  //on the resource, so a client that binds later hears nothing otherwise
  wl_shm_send_format(resource, SHARED_MEMORY_FORMAT_ARGB8888);
  wl_shm_send_format(resource, SHARED_MEMORY_FORMAT_XRGB8888);
}

void init_shared_memory() {

  wl_global_create(compositor.display, &wl_shm_interface, 1, &compositor,
                   bind_shared_memory);
}
