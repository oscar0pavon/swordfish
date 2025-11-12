#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <wayland-server.h>

typedef struct SwordfishCompositor{
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_list surfaces; 
    // libinput components
}SwordfishCompositor;

typedef struct SwordfishSurface{
    struct wl_resource *resource;
    SwordfishCompositor *compositor;
    struct wl_buffer *buffer;
    int32_t x,y;
}SwordfishSurface;

void* run_compositor(void* none);

void finish_compositor();

extern SwordfishCompositor compositor;
#endif
