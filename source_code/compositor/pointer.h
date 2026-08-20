#ifndef POINTER_H

#include "tasks.h"

extern Task *pointer_focus;
extern bool pointer_entered;

void get_pointer(WClient *client, WResource *resource, uint32_t id);

#endif
