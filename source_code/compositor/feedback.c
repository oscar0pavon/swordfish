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
  uint64_t modifer;
}FormatTable;

static int create_anon_file(size_t size) {
    int fd = memfd_create("dmabuf-feedback", MFD_CLOEXEC);
    if (fd >= 0) {
        ftruncate(fd, size);
        return fd;
    }
    printf("Can't create shared memory\n");
    return -1;
}


void send_supported_formats2(struct wl_resource *resource) {
    printf("sending supported format\n");
    
    struct wl_array formats_array;
    wl_array_init(&formats_array);

    uint32_t format = DRM_FORMAT_XRGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

    *(uint32_t *)wl_array_add(&formats_array, sizeof(uint32_t)) = format;
    *(uint64_t *)wl_array_add(&formats_array, sizeof(uint64_t)) = modifier;

    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &formats_array);

    wl_array_release(&formats_array);
}


void send_format_table(WaylandResource* resource) {
    printf("Compositor sending format table via shared memory\n");

  
    FormatTable supported_formats[] = {
        // 8-bit (XR24/AR24 are likely XRGB8888/ARGB8888 variants)
        { 0x34325258, 0, DRM_FORMAT_MOD_LINEAR }, // XR24
        { 0x34325241, 0, DRM_FORMAT_MOD_LINEAR }, // AR24
        { 0x34324152, 0, DRM_FORMAT_MOD_LINEAR }, // RA24 (likely RGBA8888)
        
        // // 10-bit (XR30/AR30 are likely XRGB2101010/ARGB2101010 variants)
        // { 0x30335258, 0, DRM_FORMAT_MOD_LINEAR }, // XR30
        // { 0x30334258, 0, DRM_FORMAT_MOD_LINEAR }, // XB30
        // { 0x30335241, 0, DRM_FORMAT_MOD_LINEAR }, // AR30
        // { 0x30334241, 0, DRM_FORMAT_MOD_LINEAR }, // AB30

        // 8-bit BGR variants
        { 0x34324258, 0, DRM_FORMAT_MOD_LINEAR }, // XB24 (likely XBGR8888)
        { 0x34324241, 0, DRM_FORMAT_MOD_LINEAR }, // AB24 (likely ABGR8888/BGRA8888)

        // // 16-bit float/half-float (XR4H, AR4H, etc. are likely R16G16B16A16F variants)
        // { 0x48345258, 0, DRM_FORMAT_MOD_LINEAR }, // XR4H
        // { 0x48345241, 0, DRM_FORMAT_MOD_LINEAR }, // AR4H
        // { 0x48344258, 0, DRM_FORMAT_MOD_LINEAR }, // XB4H
        // { 0x48344241, 0, DRM_FORMAT_MOD_LINEAR }, // AB4H
        { DRM_FORMAT_RGB565, 0, DRM_FORMAT_MOD_LINEAR },
    };
 

    size_t num_formats = sizeof(supported_formats) / sizeof(FormatTable);
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

