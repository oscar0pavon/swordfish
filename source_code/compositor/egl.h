#ifndef EGL_H
#define EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <gbm.h>

void init_egl(void);

void draw_with_egl(void);

extern EGLDisplay egl_display;
extern EGLContext egl_context;
extern EGLSurface egl_surface;

extern struct gbm_surface *display_surface;
#endif
