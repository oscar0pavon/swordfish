#include "direct_render.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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

#define MAX_MONITOR 8
PMonitor monitors[MAX_MONITOR] = {};
u8 monitors_number = 0;

void get_drm_info() {

  int connector_number;
  int encoder_number;


  drmModeConnector *current_connector;
  drmModeEncoder *current_encoder;
  drmModeCrtc *current_crtc;

  for (connector_number = 0;
       connector_number < drm_device.resources->count_connectors;
       connector_number++) {
    
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
          
          monitors[connector_number].connector = current_connector;
          monitors[connector_number].encoder = current_encoder;
          monitors[connector_number].crtc = current_crtc;
          monitors_number++;

          continue; // we don't free conector and encoder if we find a monitor


        }


        printf("Free encoder\n");
        drmModeFreeEncoder(current_encoder);
      }
    }

    printf("Free connector %i\n",connector_number);
    drmModeFreeConnector(current_connector);
  }
}

void init_direct_render(void) {


  drm_device.file_descriptor = open(DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);

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


  clean_drm();
}

void clean_drm(){
  for(int i = 0; i<monitors_number; i++){
    drmModeFreeEncoder(monitors[i].encoder);
    drmModeFreeCrtc(monitors[i].crtc);
  }
  drmModeFreeResources(drm_device.resources);
  drmDropMaster(drm_device.file_descriptor);
  close(drm_device.file_descriptor);//otherwise we can't backto our window manager
}
