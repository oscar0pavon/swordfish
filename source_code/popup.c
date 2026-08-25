#include "popup.h"

#include "desktop-server.h"

#include <stdio.h>
#include <stdlib.h>
#include <wayland-server.h>

#include "compositor.h"
#include "desktop.h"
#include "log.h"
#include "subcompositor.h"
#include "surface.h"
#include "tasks.h"

//everything a client can say about where a menu should go. it is
//client-side bookkeeping until a popup is created with it, at which point
//positioner_place() below turns the lot into one rectangle
typedef struct Positioner {
  int32_t width, height;
  int32_t anchor_x, anchor_y, anchor_width, anchor_height;
  uint32_t anchor;
  uint32_t gravity;
  uint32_t constraint_adjustment;
  int32_t offset_x, offset_y;
} Positioner;

typedef struct Popup {
  WResource *resource;
  //the surface the menu is drawn on
  Task *task;
  //and the one it hangs off, which is a window or another popup
  Task *parent;
  //the client asked for an implicit grab with xdg_popup.grab. that is not a
  //formality: a toolkit that asks for one and does not get it decides the grab
  //was broken and takes the menu down again on the button release, which looks
  //exactly like a menu that is only up while the mouse is held
  bool has_grab;
  struct wl_list link;
} Popup;

//every popup on screen, oldest first. a menu with a submenu open is two of
//them, chained parent to child
static struct wl_list popups;
static bool popups_inited;

static void popups_init(void) {

  if (popups_inited)
    return;

  wl_list_init(&popups);
  popups_inited = true;
}

bool popups_are_open(void) {

  popups_init();

  return !wl_list_empty(&popups);
}

//is this surface the popup's own, or something inside it - a subsurface of the
//menu, or a submenu hanging off it
static bool task_is_inside(Task *task, Task *popup_task) {

  for (Task *walk = task; walk; walk = walk->parent)
    if (walk == popup_task)
      return true;

  return false;
}

void popups_dismiss_outside(Task *task) {

  popups_init();

  Popup *popup, *next;

  //newest first: a submenu goes before the menu it came out of, which is the
  //order a client expects to be told about them
  wl_list_for_each_reverse_safe(popup, next, &popups, link) {

    if (task && task_is_inside(task, popup->task))
      continue;

    //popup_done is a request to tear the menu down. the client answers it by
    //destroying the xdg_popup, which is what takes this off the list - doing it
    //here as well would drop it twice
    xdg_popup_send_popup_done(popup->resource);

    log_info("Popup dismissed by a press outside it");
  }

  wl_display_flush_clients(compositor.display);
}

static void positioner_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static void positioner_set_size(WClient *client, WResource *resource,
                                int32_t width, int32_t height) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->width = width;
  positioner->height = height;
}

//the rectangle on the parent the menu is anchored to, in the parent's window
//geometry coordinates - the button the menu drops out of, usually
static void positioner_set_anchor_rect(WClient *client, WResource *resource,
                                       int32_t x, int32_t y, int32_t width,
                                       int32_t height) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->anchor_x = x;
  positioner->anchor_y = y;
  positioner->anchor_width = width;
  positioner->anchor_height = height;
}

static void positioner_set_anchor(WClient *client, WResource *resource,
                                  uint32_t anchor) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->anchor = anchor;
}

static void positioner_set_gravity(WClient *client, WResource *resource,
                                   uint32_t gravity) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->gravity = gravity;
}

static void positioner_set_constraint_adjustment(WClient *client,
                                                 WResource *resource,
                                                 uint32_t adjustment) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->constraint_adjustment = adjustment;
}

static void positioner_set_offset(WClient *client, WResource *resource,
                                  int32_t x, int32_t y) {

  Positioner *positioner = wl_resource_get_user_data(resource);
  positioner->offset_x = x;
  positioner->offset_y = y;
}

//since version 3. xdg_wm_base is advertised at 1, so neither of these can
//arrive - they still need handlers, because a NULL entry is dispatched as a
//call
static void positioner_set_reactive(WClient *client, WResource *resource) {}

static void positioner_set_parent_size(WClient *client, WResource *resource,
                                       int32_t parent_width,
                                       int32_t parent_height) {}

static void positioner_set_parent_configure(WClient *client,
                                            WResource *resource,
                                            uint32_t serial) {}

static const struct xdg_positioner_interface positioner_implementation = {
    .destroy = positioner_destroy,
    .set_size = positioner_set_size,
    .set_anchor_rect = positioner_set_anchor_rect,
    .set_anchor = positioner_set_anchor,
    .set_gravity = positioner_set_gravity,
    .set_constraint_adjustment = positioner_set_constraint_adjustment,
    .set_offset = positioner_set_offset,
    .set_reactive = positioner_set_reactive,
    .set_parent_size = positioner_set_parent_size,
    .set_parent_configure = positioner_set_parent_configure,
};

static void destroy_positioner_resource(WResource *resource) {
  free(wl_resource_get_user_data(resource));
}

void create_positioner(WClient *client, WResource *resource, uint32_t id) {

  Positioner *positioner = calloc(1, sizeof(Positioner));

  if (!positioner) {
    wl_client_post_no_memory(client);
    log_error("Can't allocate positioner");
    return;
  }

  WResource *positioner_resource = wl_resource_create(
      client, &xdg_positioner_interface, wl_resource_get_version(resource), id);

  if (!positioner_resource) {
    free(positioner);
    wl_client_post_no_memory(client);
    log_error("Can't create positioner");
    return;
  }

  wl_resource_set_implementation(positioner_resource,
                                 &positioner_implementation, positioner,
                                 destroy_positioner_resource);

  log_debug("Created positioner");
}

//the point on the anchor rectangle the menu hangs from. an anchor names an
//edge or a corner of it; none means the middle
static int32_t anchor_point_x(Positioner *positioner) {

  switch (positioner->anchor) {
  case XDG_POSITIONER_ANCHOR_LEFT:
  case XDG_POSITIONER_ANCHOR_TOP_LEFT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
    return positioner->anchor_x;
  case XDG_POSITIONER_ANCHOR_RIGHT:
  case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
    return positioner->anchor_x + positioner->anchor_width;
  default:
    return positioner->anchor_x + positioner->anchor_width / 2;
  }
}

static int32_t anchor_point_y(Positioner *positioner) {

  switch (positioner->anchor) {
  case XDG_POSITIONER_ANCHOR_TOP:
  case XDG_POSITIONER_ANCHOR_TOP_LEFT:
  case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
    return positioner->anchor_y;
  case XDG_POSITIONER_ANCHOR_BOTTOM:
  case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
    return positioner->anchor_y + positioner->anchor_height;
  default:
    return positioner->anchor_y + positioner->anchor_height / 2;
  }
}

//gravity is which way the menu grows away from that point, so it says where
//the popup's own origin lands relative to it: a menu with bottom gravity hangs
//downward and starts there, one with top gravity ends there
static int32_t gravity_offset_x(Positioner *positioner) {

  switch (positioner->gravity) {
  case XDG_POSITIONER_GRAVITY_LEFT:
  case XDG_POSITIONER_GRAVITY_TOP_LEFT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
    return -positioner->width;
  case XDG_POSITIONER_GRAVITY_RIGHT:
  case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
    return 0;
  default:
    return -positioner->width / 2;
  }
}

static int32_t gravity_offset_y(Positioner *positioner) {

  switch (positioner->gravity) {
  case XDG_POSITIONER_GRAVITY_TOP:
  case XDG_POSITIONER_GRAVITY_TOP_LEFT:
  case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
    return -positioner->height;
  case XDG_POSITIONER_GRAVITY_BOTTOM:
  case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
    return 0;
  default:
    return -positioner->height / 2;
  }
}

static int32_t clamp(int32_t value, int32_t low, int32_t high) {

  if (high < low)
    return low;

  if (value < low)
    return low;

  if (value > high)
    return high;

  return value;
}

//where the popup goes, in the parent surface's own coordinates
static void positioner_place(Positioner *positioner, DesktopSurface *parent,
                             int32_t *out_x, int32_t *out_y) {

  int32_t x = anchor_point_x(positioner) + gravity_offset_x(positioner) +
              positioner->offset_x;
  int32_t y = anchor_point_y(positioner) + gravity_offset_y(positioner) +
              positioner->offset_y;

  //the anchor rectangle is in the parent's window geometry, which starts
  //wherever the client put it inside its surface - firefox leaves a margin
  //there for the shadow it draws around the window
  x += parent->geometry_x;
  y += parent->geometry_y;

  //a menu that does not fit is slid back inside the window rather than flipped
  //to the other side of its anchor. sword has no scissor to clip a quad with,
  //so a popup hanging off the edge of the cell would be drawn over the window
  //next door. TODO flipping is what the constraint adjustment usually asks for
  //and would keep a submenu beside its parent item instead of on top of it
  if (parent->geometry_width > 0 && parent->geometry_height > 0) {
    x = clamp(x, parent->geometry_x,
              parent->geometry_x + parent->geometry_width - positioner->width);
    y = clamp(y, parent->geometry_y, parent->geometry_y +
                                         parent->geometry_height -
                                         positioner->height);
  }

  *out_x = x;
  *out_y = y;
}

static void popup_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

//the client took an implicit grab. two things follow from it, and leaving
//either one out is what makes a menu close again the moment the button comes
//up: every press outside the popup dismisses it (pointer.c calls
//popups_dismiss_outside), and the *keyboard* goes to the popup for as long as
//it is up. gtk asks for the grab and then waits to be told it has the focus;
//never being told, it treats the grab as broken and dismisses the menu itself
static void popup_grab(WClient *client, WResource *resource, WResource *seat,
                       uint32_t serial) {

  Popup *popup = wl_resource_get_user_data(resource);

  if (!popup)
    return;

  popup->has_grab = true;

  //handle_focus() picks this up on the next frame step, the same way it picks
  //up a new focused_task
  log_info("Popup took a grab");
}

//the topmost popup holding a grab, whose surface the keyboard belongs to while
//it is up. NULL when no menu is open, and then the keyboard goes back to the
//focused window on its own - handle_focus() asks every frame
Task *popup_grab_task(void) {

  popups_init();

  Popup *popup;

  //newest first: a submenu holds the grab over the menu it came out of
  wl_list_for_each_reverse(popup, &popups, link)
    if (popup->has_grab && popup->task)
      return popup->task;

  return NULL;
}

//since version 3, and xdg_wm_base is advertised at 1
static void popup_reposition(WClient *client, WResource *resource,
                             WResource *positioner, uint32_t token) {}

static const struct xdg_popup_interface popup_implementation = {
    .destroy = popup_destroy,
    .grab = popup_grab,
    .reposition = popup_reposition,
};

//libwayland destroys a client's resources in creation order, so the
//wl_surface goes before the xdg_popup made out of it. the Popup would then be
//holding a freed Task
void forget_popup_role(Task *task) {

  if (!task->popup_resource)
    return;

  Popup *popup = wl_resource_get_user_data(task->popup_resource);

  if (popup)
    popup->task = NULL;

  task->popup_resource = NULL;
}

static void destroy_popup_resource(WResource *resource) {

  Popup *popup = wl_resource_get_user_data(resource);

  if (!popup)
    return;

  wl_list_remove(&popup->link);

  //the menu is unmapped with its role. the surface itself belongs to the
  //client and may outlive this
  if (popup->task) {
    popup->task->popup_resource = NULL;
    wl_list_remove(&popup->task->parent_link);
    wl_list_init(&popup->task->parent_link);
    popup->task->parent = NULL;
    popup->task->can_draw = false;
    array_remove_element(&tasks_for_draw, popup->task);
  }

  free(popup);

  log_info("Destroyed popup");
}

void create_popup(WClient *client, WResource *resource, uint32_t id,
                  WResource *parent_resource, WResource *positioner_resource) {

  popups_init();

  DesktopSurface *desktop_surface = wl_resource_get_user_data(resource);
  Positioner *positioner = wl_resource_get_user_data(positioner_resource);

  WResource *popup_resource = wl_resource_create(
      client, &xdg_popup_interface, wl_resource_get_version(resource), id);

  if (!popup_resource) {
    wl_client_post_no_memory(client);
    log_error("Can't create popup");
    return;
  }

  //a NULL parent is a popup that will be given one through some other
  //protocol - a layer shell, which sword does not have. there is nothing to
  //hang it off, so it is dismissed the way every popup used to be
  DesktopSurface *parent =
      parent_resource ? wl_resource_get_user_data(parent_resource) : NULL;

  if (!parent || !parent->surface || !positioner) {
    wl_resource_set_implementation(popup_resource, &popup_implementation, NULL,
                                   NULL);
    xdg_popup_send_popup_done(popup_resource);
    log_warn("Popup with no parent dismissed");
    return;
  }

  Popup *popup = calloc(1, sizeof(Popup));

  if (!popup) {
    wl_client_post_no_memory(client);
    log_error("Can't allocate popup");
    return;
  }

  popup->resource = popup_resource;
  popup->task = desktop_surface->surface;
  popup->parent = parent->surface;

  //so the surface can let go of this role if the client destroys it first
  popup->task->popup_resource = popup_resource;

  wl_resource_set_implementation(popup_resource, &popup_implementation, popup,
                                 destroy_popup_resource);

  int32_t x, y;
  positioner_place(positioner, parent, &x, &y);

  //hung off the parent exactly as a subsurface is, at the tail of its children
  //so it is drawn last and hit tested first. everything that walks the tree -
  //draw_surface_tree(), pointer_hit_child(), task_screen_rect() - then treats a
  //menu as what it is: a surface inside another one
  popup->task->parent = parent->surface;
  popup->task->child_x = x;
  popup->task->child_y = y;

  wl_list_remove(&popup->task->parent_link);
  wl_list_insert(parent->surface->children.prev, &popup->task->parent_link);

  wl_list_insert(popups.prev, &popup->link);

  //the client may not draw until it has been configured, and a popup is
  //configured with the rectangle it was given
  xdg_popup_send_configure(popup_resource, x, y, positioner->width,
                           positioner->height);

  uint32_t serial = wl_display_next_serial(compositor.display);
  desktop_surface->pending_serial = serial;
  xdg_surface_send_configure(desktop_surface->resource, serial);

  log_info("Popup at %i %i, %ix%i, parent task %p in cell %i %i %ix%i", x, y,
           positioner->width, positioner->height, (void *)parent->surface,
           parent->surface->tile_x, parent->surface->tile_y,
           parent->surface->tile_width, parent->surface->tile_height);

  log_surface_tree("popup created");
}
