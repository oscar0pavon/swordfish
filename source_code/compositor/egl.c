#include "egl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <gbm.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "compositor/compositor.h"
#include "direct_render.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <drm_fourcc.h>

#include "GLES2/gl2.h"

#include "direct_render.h"
#include "swordfish.h"
#include "window.h"

EGLDisplay egl_display;
EGLContext egl_context;
EGLSurface egl_surface;
EGLConfig config;
EGLint num_config;

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


void init_egl() {

  printf("Initializing EGL\n");
  const char *egl_extensions;

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

  EGLint major, minor;


  if(is_drm_rendering){
    setenv("EGL_PLATFORM", "gbm", 1);
    egl_display =
        eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, buffer_device, NULL);
  }else{
    egl_display = 
      eglGetDisplay((EGLNativeDisplayType)display);
  }

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


  const EGLint config_attribs[] = {
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      8,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT, // Use OpenGL ES 2.0
      EGL_SURFACE_TYPE,
      EGL_WINDOW_BIT, // We want a window surface compatible with GBM
      EGL_NONE,
  };

  if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &num_config)) {
    fprintf(stderr, "Failed to choose EGL config: 0x%x\n", eglGetError());
    return;
  }

  EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, // Use EGL ES 2.0
                              EGL_NONE};

  egl_context = eglCreateContext(egl_display, config,
                                 EGL_NO_CONTEXT, context_attribs);

  if (egl_context == EGL_NO_CONTEXT) {
    fprintf(stderr, "Failed to create EGL context: 0x%x\n", eglGetError());
  }

  if(is_drm_rendering){
    egl_surface = eglCreatePlatformWindowSurface(egl_display, config,
                                                 display_surface, NULL);
  }else{
    egl_surface = eglCreateWindowSurface(egl_display, config, swordfish_window, NULL);
  }

  if (egl_surface == EGL_NO_SURFACE) {
    fprintf(stderr, "Failed to create EGL window surface: 0x%x\n",
            eglGetError());
    return;
  }
  
  unsetenv("EGL_PLATFORM"); 
  printf("Finish EGL initialization\n");

}

void draw_with_egl() {

  EGLBoolean make_current =
      eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
  
  if (!make_current)
    printf("Can't make current context\n");

  while (1) {
    glClearColor(0.3f, 0.3f, 0.9f, 1.0f); // Clear to a blue color
    glClear(GL_COLOR_BUFFER_BIT);

    glFlush();

    if (eglSwapBuffers(egl_display, egl_surface) == EGL_FALSE) {
      EGLint error = eglGetError();
      fprintf(stderr, "eglSwapBuffers failed, EGL Error: 0x%x\n", error);
    }

    if (is_drm_rendering) {

      struct gbm_bo *buffer;

      buffer = gbm_surface_lock_front_buffer(display_surface);
      if (!buffer) {
        printf("Can't get front buffer\n");
      }

      create_framebuffer(buffer);

      gbm_surface_release_buffer(display_surface, buffer);

    }
  }
}
