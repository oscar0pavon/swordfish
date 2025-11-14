#include "dma.h"
#include "linux-dmabuf.h"
#include "compositor.h"
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>

void bind_dma(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id);

void init_dma(){

  wl_global_create(compositor.display, &zwp_linux_dmabuf_v1_interface, 1,
                   &compositor, bind_dma);
}

void params_add(struct wl_client *client,
		    struct wl_resource *resource,
		    int32_t fd,
		    uint32_t plane_idx,
		    uint32_t offset,
		    uint32_t stride,
		    uint32_t modifier_hi,
        uint32_t modifier_lo){

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

void create_params(WaylandClient *client, WaylandResource *resource,
    uint32_t id) {

  WaylandResource *params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);

  wl_resource_set_implementation(params_resource, &params_implementation, NULL,
                                 NULL);

  printf("Create params\n");
}

const struct zwp_linux_dmabuf_v1_interface dmabuf_implementation = {
    .get_default_feedback = NULL, // For protocol version 4+
    .create_params = create_params, // <-- Add this here
};

void bind_dma(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id) {
  
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
