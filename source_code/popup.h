#ifndef POPUP_H
#define POPUP_H

#include "types.h"

#include <stdint.h>

//xdg_positioner and xdg_popup exist so that a client opening a menu does not
//take the compositor down with it. swordfish draws one quad per toplevel and
//has nowhere to put a second surface, so the popup is created and immediately
//dismissed
void create_positioner(WClient *client, WResource *resource, uint32_t id);

void create_popup(WClient *client, WResource *resource, uint32_t id,
                  WResource *parent, WResource *positioner);

#endif
