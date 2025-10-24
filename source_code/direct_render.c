#include "direct_render.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define DRM_DEVICE_PATH "/dev/dri/card0"


typedef struct KernelModeSettingDevice{
    int file_descriptor;
    drmModeRes* resources;
}KernelModeSettingDevice;

KernelModeSettingDevice drm_device;

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

  


  clean_drm();
}

void clean_drm(){
  drmModeFreeResources(drm_device.resources);
  drmDropMaster(drm_device.file_descriptor);
  close(drm_device.file_descriptor);//otherwise we can't backto our window manager
}
