#ifndef DIRECT_RENDER_H
#define DIRECT_RENDER_H

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>

#include <gbm.h>

#include <drm/drm_fourcc.h>

extern struct gbm_device* buffer_device;

void init_direct_render(void);

void clean_drm();

struct gbm_device *create_gbm_device(int drm_file_descriptor);

struct gbm_bo *create_gbm_buffer(struct gbm_device *gbm_dev, int width, int height);

void create_framebuffer(struct gbm_bo* in_buffer);

void get_drm_support_format();

#endif
