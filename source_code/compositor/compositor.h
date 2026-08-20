#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <libseat.h>
#include "client_buffer.h"
#include "dma.h"
#include <engine/images.h>
#include <engine/model.h>
#include "input.h"

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

typedef struct Task{
    WClient *client;
    WResource *resource;
    TaskInput* input;
    SwordfishCompositor *compositor;
    WResource * frame_call_resource;
    WResource* buffer_resource;
    PTexture *image;
    PModel model;//quad vertices
    struct wl_list link;
    int32_t x,y;
    bool can_draw;
    //a cursor image the client handed to wl_pointer.set_cursor. it arrived as
    //an ordinary surface and there is nothing to draw a cursor into yet, so it
    //is kept out of the scene
    bool is_cursor;
    //release is owed once for each buffer the client attaches, not once per
    //frame: a client told again that a buffer it has already taken back is free
    //is entitled to draw into the one being sampled
    bool buffer_released;
    //the client is free to destroy a wl_buffer while the task still points at
    //it, and wl_buffer_send_release() would write straight through the freed
    //resource
    struct wl_listener buffer_destroy;
    bool listening_to_buffer;
    //the buffer the client attached before the current one. release is owed on
    //it, but the quad went on sampling it until the moment the new one arrived,
    //so it cannot be sent until the gpu is finished with the frames that did -
    //end_frame() is the first point where that is true
    WResource *old_buffer_resource;
    struct wl_listener old_buffer_destroy;
    bool listening_to_old_buffer;
    //the buffer behind image, and the only thing that says which protocol it
    //came in on. an shm buffer has to be copied onto the gpu every commit and
    //handed straight back; a dmabuf is sampled where it lies and released only
    //when a newer one replaces it
    ClientBuffer *client_buffer;
}Task;

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

void focus_task(Task *task);

extern bool is_focus_completed;
extern bool is_opengl;

extern SwordfishCompositor compositor;
#endif
