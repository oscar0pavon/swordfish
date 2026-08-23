#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <libseat.h>
#include "client_buffer.h"
#include "dma.h"
#include <engine/images.h>
#include <engine/model.h>

#include "types.h"

//the versions swordfish advertises on the registry. a client binding above
//these is a protocol error and gets disconnected, so they have to cover what
//the clients actually ask for - pway binds wl_compositor and wl_seat at 4
#define COMPOSITOR_VERSION 4
#define SEAT_VERSION 4

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


//this thread also drives input (pway or libinput) and the render loop; see
//the stage-3 note in swordfish.c's swordfish_frame_step(). there is only one
//thread now, so nothing that sends to a client needs a lock any more
void run_compositor(void);

void finish_compositor();


uint32_t next_serial(void);

extern bool is_focus_completed;
extern bool is_opengl;

extern SwordfishCompositor compositor;
#endif
