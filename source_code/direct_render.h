#ifndef DIRECT_RENDER_H
#define DIRECT_RENDER_H

extern struct gbm_device* buffer_device;

void init_direct_render(void);

void clean_drm();

struct gbm_device *create_gbm_device(int drm_file_descriptor);

struct gbm_bo *create_gbm_buffer(struct gbm_device *gbm_dev, int width, int height);

void create_framebuffer(struct gbm_bo* in_buffer);

#endif
