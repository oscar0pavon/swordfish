#ifndef POINTER_H
#define POINTER_H

#include "tasks.h"

extern Task *pointer_focus;
extern bool pointer_entered;

//the toplevel the surface under the cursor belongs to. the two are the same
//window until a client puts its content in a subsurface, and then the pointer
//events go to the child while the focus still belongs to the window
extern Task *pointer_window;

void get_pointer(WClient *client, WResource *resource, uint32_t id);

//clears a super+drag in progress if it was dragging this task. called from
//forget_task() right before the Task itself is freed
void pointer_forget_task(Task *task);

//redo the hit test against a scene that may have changed while the cursor sat
//still, and send the leave and enter that follows. once a frame
void pointer_refresh_focus(void);

#endif
