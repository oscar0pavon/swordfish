#include "dma.h"
#include "linux-dmabuf.h"
#include "compositor.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>

void bind_dma(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id);


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

  printf("########Create params before\n");
  WaylandResource *params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface, 1, id);

  wl_resource_set_implementation(params_resource, &params_implementation, NULL,
                                 NULL);

  printf("Create params\n");
}

void create_feedback(WaylandClient *client, WaylandResource *resource,
    uint32_t id) {

  WaylandResource *feedback = wl_resource_create(
      client, &zwp_linux_dmabuf_feedback_v1_interface, 1, id);

  // int feedback_fd = dup(compositor.gpu_fd);
  //
  // zwp_linux_dmabuf_feedback_v1_send_main_device(feedback, feedback_fd);

  printf("Get feed back\n");


}

const struct zwp_linux_dmabuf_v1_interface dmabuf_implementation = {
    .get_default_feedback = create_feedback,
    .create_params = create_params
};

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

  wl_global_create(compositor.display, &zwp_linux_dmabuf_v1_interface, 1,
                   &compositor, bind_dma);
}
