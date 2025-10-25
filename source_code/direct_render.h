#ifndef DIRECT_RENDER_H
#define DIRECT_RENDER_H

extern struct gbm_device* buffer_device;

void init_direct_render(void);

void clean_drm();

#endif
