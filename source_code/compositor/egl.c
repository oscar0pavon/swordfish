#include "egl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "compositor/compositor.h"
#include "direct_render.h"


EGLDisplay egl_display;
EGLContext egl_context;


void init_egl() {

  printf("Initializing EGL\n");
  const char *egl_extensions;

  buffer_device = gbm_create_device(compositor.gpu_fd);

  EGLint major, minor;

  setenv("EGL_PLATFORM", "gbm", 1);

  egl_display =
      eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, buffer_device, NULL);

  unsetenv("EGL_PLATFORM"); 

  if (egl_display == EGL_NO_DISPLAY) {
    fprintf(stderr,
            "Failed to get EGL display with eglGetPlatformDisplay: 0x%x\n",
            eglGetError());
    return;
  }

  if (!eglInitialize(egl_display, &major, &minor)) {
    fprintf(stderr, "Failed to initialize EGL: 0x%x\n", eglGetError());
    return;
  }
  printf("EGL initialized successfully (Version %d.%d).\n", major, minor);

  egl_extensions = eglQueryString(egl_display, EGL_EXTENSIONS);
  if(!egl_extensions){
    fprintf(stderr,
            "NONE extension\n");
    return;
  }else{
    //printf("EXTENSION: %s\n",egl_extensions);
  }

  EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, // Use EGL ES 2.0
                              EGL_NONE};

  egl_context = eglCreateContext(egl_display, EGL_NO_CONFIG_KHR,
                                 EGL_NO_CONTEXT, context_attribs);
  if (egl_context == EGL_NO_CONTEXT) {
    fprintf(stderr, "Failed to create EGL context: 0x%x\n", eglGetError());
  }

  return;


}
