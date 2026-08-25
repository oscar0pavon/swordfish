#ifndef POPUP_H
#define POPUP_H

#include "types.h"
#include "tasks.h"

#include <stdbool.h>
#include <stdint.h>

//xdg_positioner works out where a menu goes: a rectangle on the parent to
//anchor to, which corner of it, and which way the popup grows from there
void create_positioner(WClient *client, WResource *resource, uint32_t id);

//xdg_popup, the role a menu, a dropdown or a tooltip takes. the surface is
//hung off its parent's Task like a subsurface, so the tree that draws and hit
//tests subsurfaces draws and hit tests popups with no second mechanism
void create_popup(WClient *client, WResource *resource, uint32_t id,
                  WResource *parent, WResource *positioner);

//a popup lives until the user clicks outside it. this dismisses every popup
//the given surface is not inside - pass NULL to dismiss all of them - which is
//what a press anywhere else means, and what closes a menu again
void popups_dismiss_outside(Task *task);

//the wl_surface can be destroyed before the xdg_popup made out of it, so the
//role has to let go of the Task before it is freed - the same shape as
//forget_subsurface_role()
void forget_popup_role(Task *task);

//is anything open. cheap enough to ask on every button press
bool popups_are_open(void);

//the surface the keyboard belongs to while a menu holds a grab, or NULL. the
//focus goes back to the window by itself when the menu closes, because
//handle_focus() asks this every frame
Task *popup_grab_task(void);

#endif
