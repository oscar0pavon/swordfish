#ifndef SUBCOMPOSITOR_H
#define SUBCOMPOSITOR_H

#include <stdbool.h>

#include "tasks.h"
#include "types.h"

void init_subcompositor(void);

//a surface with a parent is not a window: it is drawn as part of the tree its
//root is the window of. two roles put a surface there - a subsurface and a
//popup - and everything downstream of this treats them the same way
bool task_is_child(Task *task);

//where this surface is drawn on the virtual desktop, in the same coordinates
//the layout writes tiles in and mouse.c reports the cursor in. a window gets
//its cell; a child gets its parent's rectangle plus its own offset, scaled the
//way the parent's buffer is scaled into that rectangle. false when there is no
//size to speak of yet
bool task_screen_rect(Task *task, double *x, double *y, double *width,
                      double *height);

//dump every surface, its role, its parent and where it is placed. a window
//that is a tree of surfaces cannot be read any other way from this side
void log_surface_tree(const char *why);

//set to a number of frames to have end_frame() dump the tree that many frames
//from now: the state at drawing time rather than mid-request
extern int surface_tree_dump_countdown;

//let go of a parent and of any children, called from the wl_surface's own
//destructor. a child whose parent went away is unmapped: the protocol has
//nowhere to draw it any more
void task_detach_subsurfaces(Task *task);

//the wl_subsurface can outlive the wl_surface it was made out of - libwayland
//tears a client's resources down in creation order - so the role has to let go
//of the Task before it is freed, the same shape as the wl_buffer listeners
void forget_subsurface_role(Task *task);

#endif
