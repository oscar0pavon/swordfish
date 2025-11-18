#include "buffers.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "window.h"
#include "swordfish.h"

#include "compositor/compositor.h"

struct gbm_device* buffer_device;

struct gbm_surface *display_surface;


void create_display_buffer(){

  int width = 1920;
  int height = 1080;
  if(!is_drm_rendering){
   width = WINDOW_WIDTH;
   height = WINDOW_HEIGHT;
  }

  display_surface = gbm_surface_create(buffer_device,
      width, 
      height,
      DRM_FORMAT_XRGB8888, 
      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
 
  if(display_surface == NULL){
    printf("Can't create gbm surface\n");
  }

}

void init_buffers(){

  if(!is_drm_rendering){
     const char *drm_device_path = "/dev/dri/renderD128";
     int fd;
     fd = open(drm_device_path, O_RDWR);
     if (fd < 0) {
       perror("Failed to open DRM device");
     }else{
       compositor.gpu_fd = fd;
     }
  }

  buffer_device = create_gbm_device(compositor.gpu_fd);
  if(!buffer_device){
    printf("Can't create GBM device\n");
  }

  create_display_buffer();

}

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
