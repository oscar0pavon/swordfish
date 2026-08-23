#include "feedback.h"
#include "compositor.h"
#include "dma.h"
#include "linux-dmabuf.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <drm/drm_fourcc.h>
#include <string.h>
#include <wayland-server-protocol.h>
#include "linux-dmabuf.h"
#include "drm_format.h"
#include <engine/renderer/physical_devices.h>
#include "log.h"

typedef struct FormatTable {
  uint32_t format;
  uint32_t padding;
  uint64_t modifier;
}FormatTable;

size_t num_formats;
size_t table_size;

//the table used to be a hardcoded guess - two formats with modifier 0, one of
//which (BGRA8888) has no vulkan format with the same byte order at all, so a
//client that picked it got its channels rotated. it is built at startup now by
//asking the GPU which modifiers it can actually sample each format with
#define MAX_ADVERTISED_FORMATS 128

FormatTable supported_formats[MAX_ADVERTISED_FORMATS];

static void add_format_modifiers(const DrmFormat *format) {

  VkDrmFormatModifierPropertiesListEXT modifier_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
  };

  VkFormatProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &modifier_list,
  };

  //called twice: once for the count, once for the data
  vkGetPhysicalDeviceFormatProperties2(vk_physical_device, format->vulkan,
                                       &properties);

  if (modifier_list.drmFormatModifierCount == 0)
    return;

  VkDrmFormatModifierPropertiesEXT *modifiers =
      calloc(modifier_list.drmFormatModifierCount,
             sizeof(VkDrmFormatModifierPropertiesEXT));
  if (!modifiers)
    return;

  modifier_list.pDrmFormatModifierProperties = modifiers;

  vkGetPhysicalDeviceFormatProperties2(vk_physical_device, format->vulkan,
                                       &properties);

  for (uint32_t i = 0; i < modifier_list.drmFormatModifierCount; i++) {
    //a modifier the compositor cannot sample from is no use to it, whatever
    //else the GPU can do with it
    if (!(modifiers[i].drmFormatModifierTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
      continue;

    //every format in the table is single plane, and a multi plane modifier
    //would need one fd and one layout per plane to import
    if (modifiers[i].drmFormatModifierPlaneCount != 1)
      continue;

    if (num_formats >= MAX_ADVERTISED_FORMATS)
      break;

    supported_formats[num_formats].format = format->fourcc;
    supported_formats[num_formats].padding = 0;
    supported_formats[num_formats].modifier = modifiers[i].drmFormatModifier;
    num_formats++;
  }

  free(modifiers);
}

static int create_anon_file(size_t size) {
    int fd = memfd_create("dmabuf-feedback", MFD_CLOEXEC);
    if (fd >= 0) {
        ftruncate(fd, size);
        return fd;
    }
    log_error("Can't create shared memory");
    return -1;
}


void send_supported_formats_indices(WResource *resource) {
  log_debug("Sending format indices for the current tranche");

  struct wl_array indices_array;
  wl_array_init(&indices_array);

  for (uint32_t i = 0; i < num_formats; i++) {
    uint16_t *index_ptr =
        (uint16_t *)wl_array_add(&indices_array, sizeof(uint16_t));
    if (!index_ptr) {
      log_error("Failed to allocate memory for format index in wl_array.");
      break;
    }
    *index_ptr = (uint16_t)i;
  }

  zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &indices_array);

  wl_array_release(&indices_array);
}

void init_format_table() {
  //built once - the GPU's answer does not change, and get_feedback() is called
  //again for every client
  if (num_formats > 0)
    return;

  for (int i = 0; i < drm_format_count(); i++)
    add_format_modifiers(drm_format_get(i));

  table_size = num_formats * sizeof(FormatTable);

  log_info("Advertising %zu format/modifier pairs", num_formats);

  for (size_t i = 0; i < num_formats; i++) {
    const DrmFormat *format = drm_format_find(supported_formats[i].format);
    log_debug("  %s modifier 0x%016lx", format ? format->name : "????",
              (unsigned long)supported_formats[i].modifier);
  }
}

void send_format_table(WResource* resource) {
    log_info("Compositor sending format table via shared memory");

    int fd = create_anon_file(table_size);
    if (fd < 0) {
        log_error("Failed to create shared memory file");
        return;
    }

    void *map = mmap(NULL, table_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        log_error("Failed to mmap shared memory");
        return;
    }
    memcpy(map, supported_formats, table_size);
    munmap(map, table_size);

    
    zwp_linux_dmabuf_feedback_v1_send_format_table(resource, fd, table_size);


}

void destroy_feedback(WClient *client, WResource *resource) {

  log_info("Destroy feedback");
}

void destroy_surface_feedback(WClient *client,
                              WResource *resource) {

  log_info("Destroy surface feedback");
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

  log_info("Sending feed back");


  init_format_table();

  WResource *feedback =
      wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                         wl_resource_get_version(resource), id);


  //wl_resource_set_user_data(feedback, &compositor);


  wl_resource_set_implementation(feedback, &feedback_implementation,
                                 NULL, NULL);

  send_feedback(feedback);

  log_info("Sent feed back");


}

void get_surface_feedback(WClient *client, WResource *resource,
                          uint32_t id, WResource *surface_resource) {

  log_info("Get surface feedback");


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

  log_info("Sent surface feedback");
}
