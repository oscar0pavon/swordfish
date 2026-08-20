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


void* run_compositor(void* none);

//everything sent to a client has to be serialised: libwayland-server has no
//locking and swordfish sends from three threads. the compositor thread holds
//this across its whole dispatch, so a request handler is already inside it;
//the render and input threads take it around their own sends and flushes.
//recursive, and the outermost lock - before draw_tasks_mutex and
//focus_task_mutex, never after
void lock_wayland(void);
void unlock_wayland(void);

void finish_compositor();


uint32_t next_serial(void);

extern bool is_focus_completed;
extern bool is_opengl;

extern SwordfishCompositor compositor;
#endif
