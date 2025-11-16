#include "dma.h"
#include "compositor/feedback.h"
#include "linux-dmabuf.h"
#include "compositor.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>
#include "direct_render.h"
#include <sys/stat.h>
#include "feedback.h"

uint64_t main_device_id;

#define MAX_DMA_PLANES 4

typedef struct DMABuffer{
    WaylandClient *client;
    int32_t fds[MAX_DMA_PLANES];
    uint32_t offsets[MAX_DMA_PLANES];
    uint32_t strides[MAX_DMA_PLANES];
    uint64_t modifiers[MAX_DMA_PLANES];
    int num_planes;
    // ... other parameters like width, height, format will be set in create_immed
}DMABuffer;

uint64_t get_drm_device_id(const char *device_path) {
    struct stat st;
    if (stat(device_path, &st) < 0) {
        perror("stat device_path");
        return 0; // Error
    }
    // The device ID is a combination of major and minor numbers
    return (uint64_t)st.st_rdev; 
}

void bind_dma(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id);


void params_add(WaylandClient *client,
		    WaylandResource *resource,
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
}

void linux_dmabuf_create_immed(WaylandClient *client,
                               WaylandResource *resource,
                               uint32_t id, 
                               int32_t width, int32_t height,
                               uint32_t format, uint32_t flags) {


  
  printf("TODO: Create DMA-Buffer\n");

}

const struct zwp_linux_buffer_params_v1_interface params_implementation = {
    .destroy = params_destroy,
    .add = params_add,
    .create = params_create,
    .create_immed = linux_dmabuf_create_immed, // <-- The critical function
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
void create_params(WaylandClient *client, WaylandResource *resource,
    uint32_t id) {

  printf("Creating params\n");

  WaylandResource *params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 3, id);


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


const struct zwp_linux_dmabuf_v1_interface dmabuf_implementation = {
    .create_params = create_params, 
    .get_default_feedback = get_feedback,
    .get_surface_feedback = get_surface_feedback
};


void send_formats(WaylandResource* resource){


  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_ARGB8888); 
  zwp_linux_dmabuf_v1_send_format(resource, DRM_FORMAT_XRGB8888);

  zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_ARGB8888, 
                                    DRM_FORMAT_MOD_INVALID >> 32, 
                                    DRM_FORMAT_MOD_INVALID & 0xFFFFFFFF);

  zwp_linux_dmabuf_v1_send_modifier(resource, DRM_FORMAT_XRGB8888, 
                                    DRM_FORMAT_MOD_INVALID >> 32, 
                                    DRM_FORMAT_MOD_INVALID & 0xFFFFFFFF);
}

void bind_dma(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id) {
  
  printf("## Implementing DMA buffers\n");

  SwordfishCompositor* compositor = (SwordfishCompositor*)data;
  WaylandResource *resource;

  resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't implement DMA\n");
    return;
  }
  
  wl_resource_set_implementation(resource, &dmabuf_implementation, data, NULL);

  printf("DMA buffers implemented\n");
}


void init_dma(){

  printf("Added DMA global\n");

  main_device_id = get_drm_device_id("/dev/dri/card0");
  wl_global_create(compositor.display, &zwp_linux_dmabuf_v1_interface, 4,
                   &compositor, bind_dma);
}
