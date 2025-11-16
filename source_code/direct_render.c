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
