#include "direct_render.h"
#include <complex.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "compositor/compositor.h"
#include "tty.h"

#include <engine/numbers.h>

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
struct gbm_device* buffer_device;


#define MAX_MONITOR 8
PMonitor monitors[MAX_MONITOR] = {};
u8 monitors_number = 0;

struct gbm_bo *create_gbm_buffer(struct gbm_device *gbm_dev, int width, int height) {
    uint32_t format = GBM_FORMAT_XRGB8888;
    uint32_t usage = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;

    struct gbm_bo *bo = gbm_bo_create(gbm_dev, width, height, format, usage);
    if (!bo) {
        fprintf(stderr, "Error creating GBM buffer object\n");
    }
    return bo;
}

struct gbm_device *create_gbm_device(int drm_file_descriptor) {
    struct gbm_device *gbm_dev = gbm_create_device(drm_file_descriptor);
    if (!gbm_dev) {
        fprintf(stderr, "Error creating GBM device\n");
    }
    return gbm_dev;
}

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

void parse_format_blob(int gpu_fd, uint64_t formats_blob_id) {
  // 1. Retrieve the property blob data from the kernel
  drmModePropertyBlobPtr formats_blob =
      drmModeGetPropertyBlob(gpu_fd, formats_blob_id);

  if (!formats_blob) {
    fprintf(stderr, "Failed to get format modifier blob data\n");
    return;
  }

  // 2. Cast the blob data to the correct struct type defined in
  // <drm/drm_fourcc.h>
  const struct drm_format_modifier_blob *fmt_blob =
      (const struct drm_format_modifier_blob *)formats_blob->data;

  // The blob contains offsets to the start of the formats and modifiers lists
  const uint32_t *formats_ptr =
      (const uint32_t *)((char *)fmt_blob + fmt_blob->formats_offset);
  const struct drm_format_modifier *modifiers_ptr =
      (const struct drm_format_modifier *)((char *)fmt_blob +
                                           fmt_blob->modifiers_offset);

  uint32_t format_count = fmt_blob->count_formats;

  printf("Found %u supported formats:\n", format_count);

  // 3. Iterate through the formats and modifiers
  for (uint32_t i = 0; i < format_count; i++) {
    uint32_t format = formats_ptr[i];

    // Convert the 4-char code to a readable string for logging
    char format_str[5];
    memcpy(format_str, &format, 4);
    format_str[4] = '\0';

    printf("  Format %d: %s (0x%08x)\n", i, format_str, format);

    // You can check associated modifiers here if needed, linking back by index
    // ... logic for modifiers ...
  }

  // 4. Free the blob data when finished
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
        parse_format_blob(gpu_fd, blob_id);
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

  
  //buffer_device = create_gbm_device(drm_device.file_descriptor);

  // struct gbm_bo* buffer;
  // buffer = create_gbm_buffer(buffer_device, 1920, 1080);




  


  //drmModeRmFB(drm_device.file_descriptor, framebuffer_id);

  //gbm_bo_destroy(buffer);


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
