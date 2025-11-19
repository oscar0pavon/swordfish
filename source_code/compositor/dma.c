#include "dma.h"
#include <complex.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "engine/images.h"
#include "feedback.h"
#include "swordfish.h"
#include "direct_render.h"
#include "linux-dmabuf.h"
#include "compositor.h"

uint64_t main_device_id;


static struct zwp_linux_dmabuf_v1_interface dmabuf_data;
      

uint64_t get_drm_device_id(const char *device_path) {
    struct stat st;
    if (stat(device_path, &st) < 0) {
        perror("stat device_path");
        return 0; // Error
    }
    // The device ID is a combination of major and minor numbers
    return (uint64_t)st.st_rdev; 
}



void params_add(WClient *client,
		    WResource *resource,
		    int32_t fd,
		    uint32_t plane_idx,
		    uint32_t offset,
		    uint32_t stride,
		    uint32_t modifier_hi,
        uint32_t modifier_lo){

  printf("Adding params\n");

  DMABuffer *params = wl_resource_get_user_data(resource);

  if (!params) {
    close(fd);
    return;
  }

  if (plane_idx >= MAX_DMA_PLANES) {
    close(fd);
    return;
  }

  if (params->fds[plane_idx] != -1) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                           "Plane already added");
    return;
  }
  if (fd == -1 || fcntl(fd, F_GETFL) == -1) {
    perror("Received an invalid or closed FD from client");
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "Received invalid FD");
    return;
  }
  params->fds[plane_idx] = fd;
  params->offsets[plane_idx] = offset;
  params->strides[plane_idx] = stride;
  // Reconstruct the 64-bit modifier
  params->modifiers[plane_idx] = ((uint64_t)modifier_hi << 32) | modifier_lo;
  params->num_planes++;

  printf("Added DMA buffer plane %u with FD %d\n", plane_idx, fd);
}

void params_create(struct wl_client *client,
		       struct wl_resource *resource,
		       int32_t width,
		       int32_t height,
		       uint32_t format,
           uint32_t flags){

  printf("params create\n");
}

static void params_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource); // Client is done with temporary params object
    printf("destroy params\n");
}

void destroy_buffer_immd(WClient *client, WResource *resource){
  printf("destroy buffer\n");
}

struct wl_buffer_interface buffer_implementation = {
  .destroy = destroy_buffer_immd
};

PTexture image;

void linux_dmabuf_create_immed(WClient *client,
                               WResource *resource,
                               uint32_t buffer_id, 
                               int32_t width, int32_t height,
                               uint32_t format, uint32_t flags) {


  DMABuffer *buffer = wl_resource_get_user_data(resource);
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;

  image.width = width;
  image.heigth = height;


  WResource* buffer_resource = 
    wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);


  wl_resource_set_implementation(buffer_resource, &buffer_implementation, NULL,
                                 NULL);

  printf("Creatig buffer with %i %i\n", width, height);
  printf("Buffer fd[0]=%i\n",buffer->fds[0]);
  pe_vk_import_image(&image, width, height, buffer->fds[0], buffer->modifiers[0]);

  wl_resource_set_user_data(buffer_resource, &image);

  //finish
  for (int i = 0; i < buffer->num_planes; i++) {
    if (buffer->fds[i] != -1)
      close(buffer->fds[i]);
  }

}

const struct zwp_linux_buffer_params_v1_interface params_implementation = {
    .destroy = params_destroy,
    .add = params_add,
    .create = params_create,
    .create_immed = linux_dmabuf_create_immed
};

void destroy_params_handler(struct wl_resource *resource) {
    DMABuffer *params = wl_resource_get_user_data(resource);
    
    if (params) {
        // Close any FDs that might not have been consumed yet (e.g., if creation failed)
        for (int i = 0; i < MAX_DMA_PLANES; i++) {
            if (params->fds[i] != -1) {
                close(params->fds[i]);
            }
        }
        free(params);
    }
    printf("Destroy params\n");
}

void swordfish_create_params(WClient *client, WResource *resource,
                             uint32_t id) {

  printf("Received create_params request. Creating new buffer_params resource ID: %u\n", id);

  WResource *params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                         wl_resource_get_version(resource), id);


  DMABuffer *params = calloc(1, sizeof(DMABuffer));

  for (int i = 0; i < MAX_DMA_PLANES; i++) {
      params->fds[i] = -1;
  }

  params->client = client;

  wl_resource_set_user_data(params_resource, params);



  wl_resource_set_implementation(params_resource, &params_implementation,
                                 params, destroy_params_handler);

  printf("Created params\n");
}

static void destroy_dmabuf_resource(struct wl_resource *resource) {
    // You can retrieve and free any user data attached to this resource if necessary
    // void *data = wl_resource_get_user_data(resource);
    // free(data); // If you allocated data specifically for this resource

    printf("Destroying zwp_linux_dmabuf_v1 resource: ID %u\n", wl_resource_get_id(resource));
    // The resource itself is managed by the Wayland library, no need to free 'resource'
}

void destry_dma(struct wl_client *client,
    struct wl_resource *resource){

  printf("destry dma");
}

void bind_dma(WClient *client, void *data, uint32_t version,
                       uint32_t id) {
  
  printf("## Implementing DMA buffers\n");

  WResource *resource;

  resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't implement DMA\n");
    return;
  }
  
  dmabuf_data.destroy = destry_dma;
  dmabuf_data.create_params = swordfish_create_params;
  dmabuf_data.get_default_feedback = get_feedback;
  dmabuf_data.get_surface_feedback = get_surface_feedback;


  wl_resource_set_implementation(resource, &dmabuf_data, NULL, destroy_dmabuf_resource);

  printf("Bound zwp_linux_dmabuf_v1 global for client (ID %u, Version %u)\n", id, version);
}


void init_dma(){

  printf("Added DMA global\n");

  main_device_id = get_drm_device_id("/dev/dri/card0");

  wl_global_create(compositor.display, &zwp_linux_dmabuf_v1_interface, 4,
                   &compositor, bind_dma);

}
