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
#include <wayland-server-protocol.h>
#include "linux-dmabuf.h"

typedef struct FormatTable {
  uint32_t format;
  uint32_t padding;
  uint64_t modifier;
}FormatTable;

size_t num_formats;
size_t table_size;

#define AMD_MOD_XR24_SCANOUT 0x200000018733b03 
/* or */
#define AMD_MOD_AR24_SCANOUT 0x200000018763b03

FormatTable supported_formats[] = {
        // 8-bit UNORM formats (most commonly used)
        // { 0x34324152, 0, DRM_FORMAT_MOD_LINEAR }, // RA24 (likely RGBA8888)
        // // // // 8-bit BGR variants
        // { 0x34324258, 0, DRM_FORMAT_MOD_LINEAR }, // XB24 (likely XBGR8888)
        // { 0x34324241, 0, DRM_FORMAT_MOD_LINEAR }, // AB24 (likely ABGR8888/BGRA8888)
        // { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR }, 
        // { DRM_FORMAT_BGRA8888, 0, DRM_FORMAT_MOD_LINEAR },
        //{ DRM_FORMAT_BGRA8888, 0, AMD_FMT_MOD_DCC_MASK},
        //{ 0x34325258, 0, 0x0 }, // XR24 (XRGB8888) - Works with 0x0
        { DRM_FORMAT_XRGB8888, 0, 0x0}, // Try linear first (0x0)
        { DRM_FORMAT_BGRA8888, 0, 0x0},
       // { 0x34325241, 0, AMD_MOD_AR24_SCANOUT}, // AR24 (ARGB8888) - Works with 0x0
        // { DRM_FORMAT_RGBA8888, 0, DRM_FORMAT_MOD_LINEAR }, // PIPE_FORMAT_R8G8B8A8_UNORM
        // { DRM_FORMAT_BGRA8888, 0, DRM_FORMAT_MOD_LINEAR }, // PIPE_FORMAT_B8G8R8A8_UNORM
        // { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR }, // PIPE_FORMAT_R8G8B8X8_UNORM
        // { DRM_FORMAT_BGR888, 0, DRM_FORMAT_MOD_LINEAR },   // PIPE_FORMAT_B8G8R8_UNORM
        // { DRM_FORMAT_RGBX8888, 0, DRM_FORMAT_MOD_LINEAR }, // PIPE_FORMAT_R8G8B8X8_UNORM
        // { DRM_FORMAT_RGB888, 0, DRM_FORMAT_MOD_LINEAR },   // PIPE_FORMAT_R8G8B8_UNORM
        //
        // // // 8-bit (XR24/AR24 are likely XRGB8888/ARGB8888 variants)
        // // { 0x34325258, 0, DRM_FORMAT_MOD_LINEAR }, // XR24
        // // { 0x34325241, 0, DRM_FORMAT_MOD_LINEAR }, // AR24
        // //
        // { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR },
        // // 16-bit float/half-float (XR4H, AR4H, etc. are likely R16G16B16A16F variants)
        // { 0x48345258, 0, DRM_FORMAT_MOD_LINEAR }, // XR4H
        // { 0x48345241, 0, DRM_FORMAT_MOD_LINEAR }, // AR4H
        // { 0x48344258, 0, DRM_FORMAT_MOD_LINEAR }, // XB4H
        // { 0x48344241, 0, DRM_FORMAT_MOD_LINEAR }, // AB4H
        // // 10-bit (XR30/AR30 are likely XRGB2101010/ARGB2101010 variants)
        // { 0x30335258, 0, DRM_FORMAT_MOD_LINEAR }, // XR30
        // { 0x30334258, 0, DRM_FORMAT_MOD_LINEAR }, // XB30
        // { 0x30335241, 0, DRM_FORMAT_MOD_LINEAR }, // AR30
        // { 0x30334241, 0, DRM_FORMAT_MOD_LINEAR }, // AB30
      // 10-bit UNORM formats
        // { DRM_FORMAT_B2G10R10X2A_UNORM, 0, DRM_FORMAT_MOD_LINEAR }, // B10G10R10X2A
        // { DRM_FORMAT_B2G10R10A10_UNORM, 0, DRM_FORMAT_MOD_LINEAR }, // B10G10R10A2
        // { DRM_FORMAT_R10G10B10X2A_UNORM, 0, DRM_FORMAT_MOD_LINEAR }, // R10G10B10X2A
        // { DRM_FORMAT_R10G10B10A2_UNORM, 0, DRM_FORMAT_MOD_LINEAR }, // R10G10B10A2

        // 16-bit packed UNORM formats
        // { DRM_FORMAT_RGB565, 0, DRM_FORMAT_MOD_LINEAR },   // PIPE_FORMAT_B5G6R5_UNORM
        // { DRM_FORMAT_BGR5A1, 0, DRM_FORMAT_MOD_LINEAR },   // PIPE_FORMAT_B5G5R5A1_UNORM
        // { DRM_FORMAT_BGRX555, 0, DRM_FORMAT_MOD_LINEAR },  // PIPE_FORMAT_B5G5R5X1_UNORM
        // { DRM_FORMAT_BGRA4444, 0, DRM_FORMAT_MOD_LINEAR }, // PIPE_FORMAT_B4G4R4A4_UNORM
        // { DRM_FORMAT_BGRX4444, 0, DRM_FORMAT_MOD_LINEAR },
    };



static int create_anon_file(size_t size) {
    int fd = memfd_create("dmabuf-feedback", MFD_CLOEXEC);
    if (fd >= 0) {
        ftruncate(fd, size);
        return fd;
    }
    printf("Can't create shared memory\n");
    return -1;
}


void send_supported_formats_indices(WResource *resource) {
  printf("Sending format indices for the current tranche\n");

  struct wl_array indices_array;
  wl_array_init(&indices_array);

  for (uint32_t i = 0; i < num_formats; i++) {
    uint16_t *index_ptr =
        (uint16_t *)wl_array_add(&indices_array, sizeof(uint16_t));
    if (!index_ptr) {
      fprintf(stderr,
              "Failed to allocate memory for format index in wl_array.\n");
      break;
    }
    *index_ptr = (uint16_t)i;
  }

  zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &indices_array);

  wl_array_release(&indices_array);
}

void init_format_table() {
  num_formats = sizeof(supported_formats) / sizeof(FormatTable);
  table_size = num_formats * sizeof(FormatTable);
}

void send_format_table(WResource* resource) {
    printf("Compositor sending format table via shared memory\n");

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

void destroy_feedback(WClient *client, WResource *resource) {

  printf("Destroy feedback\n");
}

void destroy_surface_feedback(WClient *client,
                              WResource *resource) {

  printf("Destroy surface feedback\n");
}

const struct zwp_linux_dmabuf_feedback_v1_interface feedback_implementation = {
    .destroy = destroy_feedback};

const struct zwp_linux_dmabuf_feedback_v1_interface
    surface_feedback_implementation = {.destroy = destroy_surface_feedback};

void send_feedback(WResource *resource){
  struct wl_array device_array;
  wl_array_init(&device_array);
  
  uint64_t *device_id_ptr = wl_array_add(&device_array, sizeof(uint64_t));
 
  *device_id_ptr = (uint64_t)main_device_id;


  zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &device_array);


  send_format_table(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource,
                                                          &device_array);

  wl_array_release(&device_array);

  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(
      resource, 0);

  send_supported_formats_indices(resource);

  zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);

  zwp_linux_dmabuf_feedback_v1_send_done(resource);

}


void get_feedback(WClient *client, WResource *resource,
    uint32_t id) {

  printf("Sending feed back\n");


  init_format_table();

  WResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);


  //wl_resource_set_user_data(feedback, &compositor);


  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 NULL, NULL);

  send_feedback(feedback);

  printf("Sent feed back\n");


}

void get_surface_feedback(WClient *client, WResource *resource,
                          uint32_t id, WResource *surface_resource) {

  printf("Get surface feedback\n");


  // if (!surface) {
  //   fprintf(stderr, "ERROR: Client sent get_surface_feedback with NULL surface "
  //                   "resource. Terminating client connection.\n");
  //
  //   wl_resource_post_error(resource, 7,
  //                          "Cannot get feedback for a NULL surface.");
  //   return;
  // }

  WResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);
  if (!feedback) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_user_data(feedback, surface_resource);

  wl_resource_set_implementation(feedback, &surface_feedback_implementation, &compositor,
                                 NULL);

  send_feedback(feedback);

  printf("Sent surface feedback\n");
}
