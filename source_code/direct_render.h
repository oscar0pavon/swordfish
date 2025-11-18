#ifndef DIRECT_RENDER_H
#define DIRECT_RENDER_H

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>


void init_direct_render(void);

void clean_drm();


void get_drm_support_format();

#endif
