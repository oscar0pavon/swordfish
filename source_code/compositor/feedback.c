#include "feedback.h"
#include "compositor/compositor.h"
#include "dma.h"
#include "linux-dmabuf.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>
#include "direct_render.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <drm/drm_fourcc.h>
#include <string.h>


typedef struct FormatTable {
  uint32_t format;
  uint32_t padding;
  uint32_t modifer;
}FormatTable;

static int create_anon_file(size_t size) {
    int fd = memfd_create("dmabuf-feedback", MFD_CLOEXEC);
    if (fd >= 0) {
        ftruncate(fd, size);
        return fd;
    }
    // Fallback for systems without memfd_create
    // ... (omitted for brevity, but a robust compositor needs this) ...
    return -1;
}



void send_format_table(WaylandResource* resource) {
    printf("Compositor sending format table via shared memory\n");

    FormatTable supported_formats[] = {
        { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR },
    };

    size_t num_formats = sizeof(supported_formats) / sizeof(supported_formats[0]);
    size_t table_size = num_formats * sizeof(FormatTable);

    int fd = create_anon_file(table_size);
    if (fd < 0) {
        fprintf(stderr, "Failed to create shared memory file\n");
        return;
    }

  void *map = mmap(NULL, table_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        fprintf(stderr, "Failed to mmap shared memory\n");
        return;
    }
    memcpy(map, supported_formats, table_size);
    munmap(map, table_size);

    zwp_linux_dmabuf_feedback_v1_send_format_table(resource, fd, table_size);


}



void destroy_feedback_handler(WaylandClient* client, WaylandResource *resource) {
    // Free any user data attached to the feedback resource
    // wl_resource_get_user_data(...)
  printf("Destroy feed back\n");
}

const struct zwp_linux_dmabuf_feedback_v1_interface feedback_implementation = {
    .destroy = destroy_feedback_handler
};

void send_supported_formats(struct wl_resource *resource) {
    printf("sending supported format indices for tranche\n");
    
    struct wl_array indices_array;
    wl_array_init(&indices_array);

    // We assume DRM_FORMAT_XRGB8888 is at index 0 in your main format table
    uint16_t format_index_0 = 0; 
    
    // Add the index to the array
    *(uint16_t *)wl_array_add(&indices_array, sizeof(uint16_t)) = format_index_0;

    // Send the array of indices
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &indices_array);

    wl_array_release(&indices_array);
}

void send_main_device(WaylandResource* resource){

  struct wl_array device_array;
  wl_array_init(&device_array);
  
  //uint64_t main_device_id = dup(compositor.gpu_fd);
  uint64_t *device_id_ptr = wl_array_add(&device_array, sizeof(uint64_t));
 
  *device_id_ptr = (uint64_t)main_device_id;


  zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &device_array);

  
  wl_array_release(&device_array);

}

void send_surface_feedback(WaylandResource *resource){

  struct wl_array device_array;
  wl_array_init(&device_array);
  
  //uint64_t main_device_id = dup(compositor.gpu_fd);
  uint64_t *device_id_ptr = wl_array_add(&device_array, sizeof(uint64_t));
 
  *device_id_ptr = (uint64_t)main_device_id;

  zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &device_array);

  send_format_table(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource,
                                                          &device_array);

  wl_array_release(&device_array);

  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(
      resource, 0); // No specific flags needed usually


  zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);


  zwp_linux_dmabuf_feedback_v1_send_done(resource);

}

void send_dmabuf_feedback(struct wl_resource *resource) {
  struct wl_array device_array;
  wl_array_init(&device_array);
  
  //uint64_t main_device_id = dup(compositor.gpu_fd);
  uint64_t *device_id_ptr = wl_array_add(&device_array, sizeof(uint64_t));
 
  *device_id_ptr = (uint64_t)main_device_id;


  zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &device_array);



  send_format_table(resource);
  //send_supported_formats(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource,
                                                          &device_array);

  wl_array_release(&device_array);

  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(
      resource, 0);

  zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);

  zwp_linux_dmabuf_feedback_v1_send_done(resource);

}

void get_feedback(WaylandClient *client, WaylandResource *resource,
    uint32_t id) {

  printf("Sending feed back\n");

  
  WaylandResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);


  wl_resource_set_user_data(feedback, &compositor);

  send_dmabuf_feedback(feedback);

  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 &compositor, NULL);



  printf("Sent feed back\n");


}


void get_surface_feedback(WaylandClient *client,
				     WaylandResource *resource,
				     uint32_t id,
             WaylandResource *surface){

  printf("Get surface feedback\n");

  if(!surface){
    printf("ERROR surface is NULL\n");
  }

  WaylandResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);
  if (!feedback) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_user_data(feedback, surface);


  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 surface, NULL);
  
  send_surface_feedback(feedback);

  printf("Sent surface feedback\n");


}

