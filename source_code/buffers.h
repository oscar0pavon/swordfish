#ifndef BUFFERS_H
#define BUFFERS_H



#include <gbm.h>

#include <drm/drm_fourcc.h>

extern struct gbm_surface *display_surface;
extern struct gbm_device* buffer_device;

struct gbm_device *create_gbm_device(int drm_file_descriptor);

struct gbm_bo *create_gbm_buffer(struct gbm_device *gbm_dev, int width, int height);

void create_framebuffer(struct gbm_bo* in_buffer);

void init_buffers();

#endif
