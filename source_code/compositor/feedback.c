#include "feedback.h"
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

void send_surface_format_table(WaylandResource* resource) {
    printf("Compositor sending surface format table via shared memory\n");

    FormatTable supported_formats[] = {
        { DRM_FORMAT_RGBA8888, 0, DRM_FORMAT_MOD_LINEAR },
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
}

const struct zwp_linux_dmabuf_feedback_v1_interface feedback_implementation = {
    .destroy = destroy_feedback_handler
};

void send_supported_formats(struct wl_resource *resource) {
    printf("sending supported format\n");
    
    struct wl_array formats_array;
    wl_array_init(&formats_array);

    uint32_t format = DRM_FORMAT_ARGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

    // Use wl_array_add correctly
    *(uint32_t *)wl_array_add(&formats_array, sizeof(uint32_t)) = format;
    *(uint64_t *)wl_array_add(&formats_array, sizeof(uint64_t)) = modifier;

    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &formats_array);

    wl_array_release(&formats_array);
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

void send_dmabuf_feedback(struct wl_resource *resource) {
  struct wl_array device_array;
  wl_array_init(&device_array);
  
  //uint64_t main_device_id = dup(compositor.gpu_fd);
  uint64_t *device_id_ptr = wl_array_add(&device_array, sizeof(uint64_t));
 
  *device_id_ptr = (uint64_t)main_device_id;


  zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &device_array);


  send_format_table(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource,
                                                          &device_array);

  send_supported_formats(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(
      resource, 0); // No specific flags needed usually


  zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);

  wl_array_release(&device_array);

  zwp_linux_dmabuf_feedback_v1_send_done(resource);
}

void get_feedback(WaylandClient *client, WaylandResource *resource,
    uint32_t id) {

  WaylandResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);


  wl_resource_set_user_data(feedback, &compositor);

  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 &compositor, NULL);

  printf("Sending feed back\n");

  send_dmabuf_feedback(feedback);

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


  wl_resource_set_user_data(feedback, surface);


  send_surface_format_table(feedback);

  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 surface, NULL);

  printf("Sent surface feedback\n");


}

