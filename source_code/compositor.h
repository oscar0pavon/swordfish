#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include "client_buffer.h"
#include "dma.h"
#include <engine/images.h>
#include <engine/model.h>

#include "types.h"

//the versions sword advertises on the registry. a client binding above
//these is a protocol error and gets disconnected, so they have to cover what
//the clients actually ask for - pway binds wl_compositor and wl_seat at 4
#define COMPOSITOR_VERSION 4
#define SEAT_VERSION 4

typedef struct SwordCompositor{
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_list surfaces; 
    DesktopBase *desktop_base;
    //INFO the DRM fd itself is not here: tty.c opens it, holds master on it
    //and hands it out through tty_drm_fd(), because dropping and retaking it
    //is part of the VT session and nothing else may do it behind that file's
    //back. this is only which device to open
    const char *gpu_path;
    struct wl_list tasks_input;

    // libinput components
}SwordCompositor;


//this thread also drives input (libinput) and the render loop; see
//the stage-3 note in sword.c's sword_frame_step(). there is only one
//thread now, so nothing that sends to a client needs a lock any more
void run_compositor(void);

void finish_compositor();

void init_compositor(void);

uint32_t next_serial(void);

extern bool is_focus_completed;
extern bool is_opengl;
extern bool sword_running;

extern SwordCompositor compositor;
#endif
