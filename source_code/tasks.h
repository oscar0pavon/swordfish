#ifndef TASKS_H
#define TASKS_H

#include "types.h"

#include "compositor.h"
#include "region.h"

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
    SwordCompositor *compositor;
    WResource * frame_call_resource;
    WResource* buffer_resource;
    PTexture *image;
    PModel model;//quad vertices
    struct wl_list link;
    int32_t x,y;
    bool can_draw;
    //which SwordOutput this window is tiled on, decided once at map time
    //from wherever the cursor was - an index into sword_outputs, and
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
    //the rectangle this window is drawn in, in the render target's own
    //pixels: the space draw_surface() draws in and the space mouse.c reports
    //the cursor in. nothing to do with x,y above, which is the attach offset.
    //a zero width means nothing has placed this window yet and the quad falls
    //back to its own buffer size. for a tiled window the layout owns these
    //four; for a floating one layout_toggle_floating() and the super+drag in
    //pointer.c do, and layout.c never writes them again until it floats back
    int32_t tile_x, tile_y, tile_width, tile_height;
    //wl_surface.set_input_region: where on this surface the client is willing
    //to be pointed at, in its own coordinates. no region set means all of it,
    //which is the protocol's default and what every window wants - but a
    //client that hands over an *empty* one means it, and firefox does: the
    //subsurface it renders into takes no input so that the pointer reaches the
    //window behind it. ignoring this is a mouse that does nothing
    Region input_region;
    bool has_input_region;
    //wl_subcompositor: the surface this one was made a child of, and the
    //children it was made the parent of. a subsurface is not a window - it
    //takes no cell in the layout and gets no toplevel - it is drawn inside its
    //parent's cell at child_x/y, above the parent. firefox puts its whole
    //rendering container in one, so this is not an optional corner of the
    //protocol. see subcompositor.c
    struct Task *parent;
    //children in stacking order, back to front: the tail is the topmost.
    //place_above/place_below reorder it
    struct wl_list children;
    //this task's link into parent->children. inited in create_surface() even
    //for a surface that never becomes a child, so unlinking is always safe
    struct wl_list parent_link;
    //where the child's origin sits in the parent's surface coordinates. both
    //roles that hang a surface off another one use it: a subsurface puts it
    //there with set_position, a popup has it worked out from its positioner
    int32_t child_x, child_y;
    //the wl_subsurface resource, and what says this surface has that role
    WResource *subsurface_resource;
    //the xdg_popup resource, the other role that makes a surface a child of
    //another one. a menu, a dropdown or a tooltip
    WResource *popup_resource;
    //set_sync/set_desync. recorded and not yet acted on - every commit is
    //applied where it arrives, see subcompositor.c
    bool subsurface_synchronized;
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
    //pulled out of the tiling and given its own rectangle - the tile_* four
    //above - to sit on top of it at. layout_apply_output() skips it,
    //draw_surfaces() and pointer_hit_task() both give it priority over every
    //tiled window. see layout_toggle_floating()
    //
    //INFO on the end of the struct on purpose, the same rule pway's header
    //carries: neither Makefile tracks header dependencies, so a `make` after
    //this file changes relinks stale objects built against the previous
    //layout. added in the middle - it was, next to tile_* - every field after
    //it moves, and surface.c goes on writing top_level at the offset it was
    //compiled with while sword.c reads it at the new one. on the end, a
    //stale object is merely one that never heard of floating. `make clean` in
    //sword after touching this file regardless
    bool is_floating;
}Task;

extern Task *focused_task;

void focus_task(Task *task);

TaskInput *task_resolve_input(Task *task);

#endif
