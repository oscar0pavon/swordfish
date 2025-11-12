#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-server.h>

typedef struct wl_resource WaylandResource;
typedef struct wl_client WaylandClient;
typedef struct wl_compositor_interface WaylanCompositorInterface;

typedef struct SwordfishCompositor{
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_list surfaces; 
    // libinput components
}SwordfishCompositor;

typedef struct SwordfishSurface{
    WaylandResource *resource;
    SwordfishCompositor *compositor;
    struct wl_buffer *buffer;
    struct wl_list link;
    int32_t x,y;
}SwordfishSurface;

void* run_compositor(void* none);

void finish_compositor();

extern SwordfishCompositor compositor;
#endif
