#ifndef COMPOSITOR_PRIMARY_SELECTION_H
#define COMPOSITOR_PRIMARY_SELECTION_H

#include "types.h"

//zwp_primary_selection_device_manager_v1 is version 1 - there is nothing past
//the requests below to gate on a client's bound version
#define PRIMARY_SELECTION_MANAGER_VERSION 1

void init_primary_selection();

//hand the current primary selection to a client that has just taken the
//keyboard, the same reason data_device_offer_selection() exists: a
//zwp_primary_selection_offer_v1 belongs to one client
void primary_selection_offer_selection(WClient *client);

void primary_selection_clear_selection(WClient *client);

#endif
