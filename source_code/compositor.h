#ifndef COMPOSITOR_H
#define COMPOSITOR_H

typedef struct SwordfishCompositor{
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    // libinput components
}SwordfishCompositor;

void* run_compositor(void* none);

void finish_compositor();

extern SwordfishCompositor compositor;
#endif
