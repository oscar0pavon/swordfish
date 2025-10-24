#include "direct_render.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DRM_DEVICE_PATH "/dev/dri/card0"

void init_direct_render(void) {
  int drm_file_descriptor;

  drm_file_descriptor = open(DRM_DEVICE_PATH, O_RDWR | O_CLOEXEC);

  if (drm_file_descriptor < 0) {
    fprintf(stderr, "Error opening DRM device %s: %s\n", DRM_DEVICE_PATH,
            strerror(errno));
    return;
  }
  
  printf("Successfully opened DRM device %s with file descriptor %d.\n",
         DRM_DEVICE_PATH, drm_file_descriptor);


  close(drm_file_descriptor);//otherwise we can't backto our window manager

}
