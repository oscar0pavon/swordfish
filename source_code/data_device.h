#ifndef COMPOSITOR_DATA_DEVICE_H
#define COMPOSITOR_DATA_DEVICE_H

#include "types.h"

//version 3, which is what the toolkits bind. everything version 3 adds is
//drag-and-drop actions, and every one of those requests has a handler below -
//libwayland dispatches a NULL entry as a call
#define DATA_DEVICE_MANAGER_VERSION 3

void init_data_device();

//hand the current selection to a client that has just taken the keyboard. a
//wl_data_offer belongs to one client, so the selection has to be offered again
//on every focus change rather than once when it is set
void data_device_offer_selection(WClient *client);

//and take it back when the client loses the keyboard: the protocol has a
//client destroy its offer on a NULL selection, and only the focused client is
//entitled to a non-NULL one
void data_device_clear_selection(WClient *client);

#endif
