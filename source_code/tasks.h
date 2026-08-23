#ifndef TASKS_H
#define TASKS_H

#include "types.h"

#include "compositor.h"

typedef struct TaskInput {
  WClient *client;
  WResource *resource;
  WResource *keyboard_resource;
  WResource *pointer_resource;
  struct wl_list link;
}TaskInput;

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
    //which SwordfishOutput this window is tiled on, decided once at map time
    //from wherever the cursor was - an index into swordfish_outputs, and
    //what both layout_apply() and draw_surfaces() filter on
    int output_index;
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
    //the retire frame counter at the moment the buffer was replaced, which is
    //the last frame that can have sampled it. the release waits until the gpu
    //is past that frame - see task_release_old_buffer()
    uint64_t old_buffer_frame;
    //the buffer behind image, and the only thing that says which protocol it
    //came in on. an shm buffer has to be copied onto the gpu every commit and
    //handed straight back; a dmabuf is sampled where it lies and released only
    //when a newer one replaces it
    ClientBuffer *client_buffer;
    //the rectangle the layout gave this window, in the render target's own
    //pixels: the space draw_surface() draws in and the space mouse.c reports
    //the cursor in. nothing to do with x,y above, which is the attach offset.
    //a zero width means the layout has not reached it yet and the quad falls
    //back to its own buffer size
    int32_t tile_x, tile_y, tile_width, tile_height;
    //the toplevel this surface became a window through, NULL while it is only
    //a wl_surface - a cursor image never gets one. what the layout counts, and
    //what a close is sent on
    struct TopLevel *top_level;
    //the xdg_toplevel is a resource in its own right and dies on its own
    //schedule: libwayland tears a disconnecting client's resources down in the
    //order they were created, so the wl_surface goes first and the toplevel's
    //destructor cannot reach back through it. the same shape as the wl_buffer
    //listeners above, and for the same reason
    struct wl_listener top_level_destroy;
    bool listening_to_top_level;
}Task;

extern Task *focused_task;

void focus_task(Task *task);

#endif
