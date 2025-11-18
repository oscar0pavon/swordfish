#include "direct_render.h"
#include <complex.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "tty.h"
#include "buffers.h"
#include "compositor/compositor.h"

#include <engine/numbers.h>
#include "swordfish.h"
#include "window.h"

#define DRM_DEVICE_PATH "/dev/dri/card0"


typedef struct KernelModeSettingDevice{
    int file_descriptor;
    drmModeRes* resources;
}KernelModeSettingDevice;

typedef struct PMonitor{
  drmModeConnector *connector;
  drmModeEncoder *encoder;
  drmModeCrtc *crtc;
}PMonitor;

KernelModeSettingDevice drm_device;

drmModeCrtcPtr original_crtc;



#define MAX_MONITOR 8
PMonitor monitors[MAX_MONITOR] = {};
u8 monitors_number = 0;


/**
 * Helper function to find the ID of a property given its name.
 */
static uint32_t get_property_id_by_name(int fd, drmModeObjectPropertiesPtr props, const char *prop_name) {
    for (uint32_t i = 0; i < props->count_props; i++) {
        drmModePropertyPtr property = drmModeGetProperty(fd, props->props[i]);
        if (property && strcmp(property->name, prop_name) == 0) {
            uint32_t id = property->prop_id;
            drmModeFreeProperty(property);
            return id;
        }
        if (property) {
            drmModeFreeProperty(property);
        }
    }
    return 0; // Return 0 if not found
}

/**
 * Retrieves the blob ID that contains the format information for a plane.
 */
uint64_t get_formats_blob_id(int gpu_fd, uint32_t plane_id) {
    drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(gpu_fd, plane_id, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        fprintf(stderr, "Failed to get properties for plane %u\n", plane_id);
        return 0;
    }

    // Find the property ID for "IN_FORMATS" or "FB_FORMATS"
    // "IN_FORMATS" is used for primary/cursor planes with format modifiers
    uint32_t formats_prop_id = get_property_id_by_name(gpu_fd, props, "IN_FORMATS");
    
    if (formats_prop_id == 0) {
       // Fallback for older drivers/different properties if "IN_FORMATS" is missing
       formats_prop_id = get_property_id_by_name(gpu_fd, props, "FB_FORMATS");
    }

    if (formats_prop_id != 0) {
        // Find the index of that property ID in the values array
        for (uint32_t i = 0; i < props->count_props; i++) {
            if (props->props[i] == formats_prop_id) {
                // Return the actual value associated with that property index
                uint64_t formats_blob_id = props->prop_values[i];
                drmModeFreeObjectProperties(props);
                return formats_blob_id;
            }
        }
    }

    drmModeFreeObjectProperties(props);
    return 0;
}

void parse_format_blob_detailed(int gpu_fd, uint64_t formats_blob_id) {
  drmModePropertyBlobPtr formats_blob =
      drmModeGetPropertyBlob(gpu_fd, formats_blob_id);

  if (!formats_blob) {
    fprintf(stderr, "Failed to get format modifier blob data\n");
    return;
  }

  const struct drm_format_modifier_blob *fmt_blob =
      (const struct drm_format_modifier_blob *)formats_blob->data;

  const uint32_t count_formats = fmt_blob->count_formats;
  const uint32_t count_modifiers = fmt_blob->count_modifiers;

  const uint32_t *formats_ptr =
      (const uint32_t *)((char *)fmt_blob + fmt_blob->formats_offset);

  const struct drm_format_modifier *modifiers_ptr =
      (const struct drm_format_modifier *)((char *)fmt_blob +
                                           fmt_blob->modifiers_offset);


  printf("Found %u formats and %u modifiers in blob:\n", count_formats,
         count_modifiers);

  // Iterate over every single modifier available
  for (uint32_t j = 0; j < count_modifiers; j++) {
    const struct drm_format_modifier *mod_entry = &modifiers_ptr[j];
    uint64_t modifier_value = mod_entry->modifier;

    printf("  Modifier 0x%llx supports formats: ",
           (unsigned long long)modifier_value);

    // Iterate over the bitmask to find which formats this modifier applies to
    for (uint32_t i = 0; i < count_formats; i++) {
      // The logic: Check if the 'i-th' bit is set in the 64-bit 'formats' field
      // of the modifier entry
      if ((mod_entry->formats >> i) & 1) {
        uint32_t format = formats_ptr[i];
        char format_str[5] = {0};
        memcpy(format_str, &format, 4);

        printf("%s ", format_str);

        // --- Store this format/modifier pair globally here ---
        // global_formats_list[global_format_count].format = format;
        // global_formats_list[global_format_count].modifier = modifier_value;
        // global_format_count++;
      }
    }
    printf("\n");
  }

  drmModeFreePropertyBlob(formats_blob);
}

void parse_format_blob(int gpu_fd, uint64_t formats_blob_id) {

  drmModePropertyBlobPtr formats_blob =
      drmModeGetPropertyBlob(gpu_fd, formats_blob_id);

  if (!formats_blob) {
    fprintf(stderr, "Failed to get format modifier blob data\n");
    return;
  }

  const struct drm_format_modifier_blob *fmt_blob =
      (const struct drm_format_modifier_blob *)formats_blob->data;

  const uint32_t *formats_ptr =
      (const uint32_t *)((char *)fmt_blob + fmt_blob->formats_offset);
  const struct drm_format_modifier *modifiers_ptr =
      (const struct drm_format_modifier *)((char *)fmt_blob +
                                           fmt_blob->modifiers_offset);

  uint32_t format_count = fmt_blob->count_formats;
  uint32_t modifier_count = fmt_blob->count_modifiers; 

  printf("Found %u supported formats:\n", format_count);

  for (uint32_t i = 0; i < format_count; i++) {
    uint32_t format = formats_ptr[i];

    // Convert the 4-char code to a readable string for logging
    char format_str[5];
    memcpy(format_str, &format, 4);
    format_str[4] = '\0';

    printf("  Format %d: %s (0x%08x)\n", i, format_str, format);

    for (uint32_t j = 0; j < modifier_count; j++) {
      // Check if the current format index is within the valid range for this
      // modifier entry
      if (modifiers_ptr[j].formats >= i && modifiers_ptr[j].formats < (i + 1)) {
        // The 'formats' field in struct drm_format_modifier is an offset
        // relative to formats_offset This part of the logic is complex and
        // usually requires a helper library to correctly map indices.

        // The easiest approach is to simply use the 'DRM_FORMAT_MOD_LINEAR'
        // first and rely on the EGL client logs to tell you which specific
        // modifier value to add.
      }
    }
  }

  drmModeFreePropertyBlob(formats_blob);
}


void get_drm_support_format() {
  int gpu_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
  drmModePlaneResPtr plane_res = drmModeGetPlaneResources(gpu_fd);
  if (!plane_res) {
    fprintf(stderr, "Failed to get plane resources\n");
    return;
  }

  for (uint32_t i = 0; i < plane_res->count_planes; i++) {
    uint32_t plane_id = plane_res->planes[i];
    
    
    int64_t blob_id = get_formats_blob_id(gpu_fd, plane_id);
    if (blob_id != 0) {
        printf("Successfully retrieved formats blob ID: %llu\n", blob_id);
        // You can now call drmModeGetPropertyBlob(gpu_fd, blob_id) to get the data
        //parse_format_blob(gpu_fd, blob_id);
        parse_format_blob_detailed(gpu_fd, blob_id);
    }
    close(gpu_fd);

   

  }

  close(gpu_fd);

  printf("Got Supported format\n");
}

void get_drm_info() {

  int connector_number;
  int encoder_number;


  drmModeConnector *current_connector;
  drmModeEncoder *current_encoder;
  drmModeCrtc *current_crtc;

  for (connector_number = 0;
       connector_number < drm_device.resources->count_connectors;
       connector_number++) {
    bool connector_mode_connected = false;
    
    current_connector =
        drmModeGetConnector(drm_device.file_descriptor,
                            drm_device.resources->connectors[connector_number]);
    
    if(current_connector->connection == DRM_MODE_CONNECTED &&
        current_connector->count_modes > 0) {
      printf("Monitor connected %i\n",connector_number);

      for (encoder_number = 0;
           encoder_number < current_connector->count_encoders;
           encoder_number++) {
        
        current_encoder =
            drmModeGetEncoder(drm_device.file_descriptor,
                              current_connector->encoders[encoder_number]);

        //CRTC - Cathode Ray Tube Controller 
        if(current_encoder->crtc_id){
          current_crtc = drmModeGetCrtc(drm_device.file_descriptor,
                                        current_encoder->crtc_id);
          
          monitors[monitors_number].connector = current_connector;
          monitors[monitors_number].encoder = current_encoder;
          monitors[monitors_number].crtc = current_crtc;
          monitors_number++;

          connector_mode_connected = true;

          continue; // we don't free conector and encoder if we find a monitor


        }


        printf("Free encoder\n");
        drmModeFreeEncoder(current_encoder);
      }
    }
    if(connector_mode_connected == false){

      printf("Free connector %i\n",connector_number);
      drmModeFreeConnector(current_connector);
    }
  }
}

void init_direct_render(void) {

  //drm_device.file_descriptor = open(DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);
  drm_device.file_descriptor = compositor.gpu_fd;

  if (drm_device.file_descriptor < 0) {
    fprintf(stderr, "Error opening DRM device %s: %s\n", DRM_DEVICE_PATH,
            strerror(errno));
    return;
  }
  
  printf("Successfully opened DRM device %s with file descriptor %d.\n",
         DRM_DEVICE_PATH, drm_device.file_descriptor);

  if (drmSetMaster(drm_device.file_descriptor) < 0) {
    fprintf(stderr, "Failed to become DRM master: %m\n");
    close(drm_device.file_descriptor);
  }

  drm_device.resources = drmModeGetResources(drm_device.file_descriptor);
  if (!drm_device.resources) {
    fprintf(stderr, "Failed to get DRM resources: %m\n");
    close(drm_device.file_descriptor);
  }

  get_drm_info();

  printf("width %i\n",monitors[0].crtc->mode.hdisplay);
  printf("height %i\n",monitors[0].crtc->mode.vdisplay);
  printf("Name %s\n",monitors[0].crtc->mode.name);

  u32 original_crtc_id = monitors[0].crtc->crtc_id;
  original_crtc = drmModeGetCrtc(drm_device.file_descriptor, original_crtc_id);

  

}

void create_framebuffer(struct gbm_bo* in_buffer){

  u32 framebuffer_id;
  int ret = drmModeAddFB(compositor.gpu_fd,
      1920, 1080, 24, 32,
      gbm_bo_get_stride(in_buffer),
      gbm_bo_get_handle(in_buffer).u32, 
      &framebuffer_id);
  if(ret != 0)
    perror("drmModeAddFB failed");



  ret = drmModeSetCrtc(drm_device.file_descriptor,
      monitors[0].crtc->crtc_id,
      framebuffer_id,
      0,
      0,
      &monitors[0].connector->connector_id,
      1,
      &monitors[0].crtc->mode);
  if(ret != 0)
    perror("drmModeSetCrtc failed");
}

void clean_drm(){
  
  gbm_device_destroy(buffer_device);

  int ret = drmModeSetCrtc(
      drm_device.file_descriptor, original_crtc->crtc_id, original_crtc->buffer_id, 
      original_crtc->x,
      original_crtc->y, &monitors[0].connector->connector_id, 1, &original_crtc->mode);

  if (ret != 0)
    perror("drmModeSetCrtc failed");

  for(int i = 0; i<monitors_number; i++){
    drmModeFreeEncoder(monitors[i].encoder);
    drmModeFreeCrtc(monitors[i].crtc);
  }
  drmModeFreeResources(drm_device.resources);
  drmDropMaster(drm_device.file_descriptor);
  close(drm_device.file_descriptor);//otherwise we can't backto our window manager
}
