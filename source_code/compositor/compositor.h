#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <libseat.h>
#include "dma.h"
#include "engine/images.h"
#include "engine/model.h"
#include "input.h"

#include "types.h"

typedef struct SwordfishCompositor{
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_list surfaces; 
    DesktopBase *desktop_base;
    struct libseat *seat;
    int gpu_fd;
    const char *gpu_path;
    int seat_active;
    int seat_fd;
    struct wl_list tasks_input;

    // libinput components
}SwordfishCompositor;

typedef struct Task{
    WClient *client;
    WResource *resource;
    TaskInput* input;
    SwordfishCompositor *compositor;
    WResource * frame_call_resource;
    PTexture *image;
    PModel model;//quad vertices
    struct wl_list link;
    int32_t x,y;
}Task;

void* run_compositor(void* none);

void finish_compositor();

void focus_task(Task *task);

extern bool is_focus_completed;
extern bool is_opengl;

extern SwordfishCompositor compositor;
#endif
