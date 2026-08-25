#include "subcompositor.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "compositor.h"
#include "log.h"
#include "surface.h"

bool task_is_subsurface(Task *task) { return task->parent != NULL; }

//the surface's own size, which is the size of the buffer it last attached.
//a parent that has not drawn anything yet still has a rectangle - its cell -
//so fall back to that rather than to zero, or every child of it would be
//scaled by nothing
static void task_surface_size(Task *task, double fallback_width,
                              double fallback_height, double *width,
                              double *height) {

  if (task->image && task->image->width > 0 && task->image->heigth > 0) {
    *width = task->image->width;
    *height = task->image->heigth;
    return;
  }

  *width = fallback_width;
  *height = fallback_height;
}

bool task_screen_rect(Task *task, double *x, double *y, double *width,
                      double *height) {

  if (!task_is_subsurface(task)) {

    //a window's cell, exactly what draw_surface() used to read straight out of
    //the task
    if (task->tile_width > 0) {
      *x = task->tile_x;
      *y = task->tile_y;
      *width = task->tile_width;
      *height = task->tile_height;
      return true;
    }

    //a surface the layout never reached is drawn at the corner at its own
    //size - the old fallback, kept so a client without a toplevel still shows
    if (!task->image)
      return false;

    *x = 0;
    *y = 0;
    *width = task->image->width;
    *height = task->image->heigth;
    return true;
  }

  double parent_x, parent_y, parent_width, parent_height;

  if (!task_screen_rect(task->parent, &parent_x, &parent_y, &parent_width,
                        &parent_height))
    return false;

  //the parent's buffer is stretched into its cell, so a child placed at
  //surface coordinates has to be stretched the same way or it lands somewhere
  //other than the part of the parent it was put over. this is the same scale
  //pointer_inside() divides by, one level further in
  double parent_surface_width, parent_surface_height;
  task_surface_size(task->parent, parent_width, parent_height,
                    &parent_surface_width, &parent_surface_height);

  double scale_x = parent_width / parent_surface_width;
  double scale_y = parent_height / parent_surface_height;

  if (!task->image)
    return false;

  *x = parent_x + task->subsurface_x * scale_x;
  *y = parent_y + task->subsurface_y * scale_y;
  *width = task->image->width * scale_x;
  *height = task->image->heigth * scale_y;

  return true;
}

//a subsurface whose parent is gone has nowhere to be drawn. it keeps its
//wl_surface and its wl_subsurface - the client may still send requests through
//both - it is only unmapped
static void orphan_subsurface(Task *child) {

  wl_list_remove(&child->parent_link);
  wl_list_init(&child->parent_link);

  child->parent = NULL;
  child->can_draw = false;

  array_remove_element(&tasks_for_draw, child);
}

void task_detach_subsurfaces(Task *task) {

  if (task->parent) {
    wl_list_remove(&task->parent_link);
    wl_list_init(&task->parent_link);
    task->parent = NULL;
  }

  //_safe: orphan_subsurface() takes each child off the list being walked
  Task *child, *next;
  wl_list_for_each_safe(child, next, &task->children, parent_link)
      orphan_subsurface(child);
}

static void subsurface_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

//the child's origin in the parent's surface coordinates. the protocol caches
//this until the parent commits; sword applies it where it arrives, for the
//same reason set_sync does nothing - see the note on the interface below
static void subsurface_set_position(WClient *client, WResource *resource,
                                    int32_t x, int32_t y) {

  Task *child = wl_resource_get_user_data(resource);
  if (!child)
    return;

  child->subsurface_x = x;
  child->subsurface_y = y;

  log_debug("Subsurface positioned at %i %i", x, y);
}

//move the child in its parent's stacking order. sibling is another child of
//the same parent, or the parent itself - which is the common case, and the one
//firefox uses to put its rendering container over the gtk surface
static void subsurface_place(WResource *resource, WResource *sibling_resource,
                             bool above) {

  Task *child = wl_resource_get_user_data(resource);
  if (!child || !child->parent)
    return;

  Task *sibling = wl_resource_get_user_data(sibling_resource);
  if (!sibling)
    return;

  Task *parent = child->parent;

  //placing against the parent itself: the list holds children only, so the
  //two cases are the ends of it. below the parent is drawn before it, which
  //the tree walk gets by ordering the list back to front
  if (sibling == parent) {
    wl_list_remove(&child->parent_link);
    if (above)
      wl_list_insert(parent->children.prev, &child->parent_link);
    else
      wl_list_insert(&parent->children, &child->parent_link);
    return;
  }

  //a sibling from another parent is a protocol error the client would be
  //killed for; sword has nowhere to put it and leaves the order alone
  if (sibling->parent != parent) {
    log_warn("Subsurface placed against a surface that is not a sibling");
    return;
  }

  wl_list_remove(&child->parent_link);

  if (above)
    wl_list_insert(&sibling->parent_link, &child->parent_link);
  else
    wl_list_insert(sibling->parent_link.prev, &child->parent_link);
}

static void subsurface_place_above(WClient *client, WResource *resource,
                                   WResource *sibling) {
  subsurface_place(resource, sibling, true);
}

static void subsurface_place_below(WClient *client, WResource *resource,
                                   WResource *sibling) {
  subsurface_place(resource, sibling, false);
}

//TODO synchronized mode means the child's commits are cached and applied with
//the parent's next commit, so a client can move a child and repaint it in one
//atomic update. sword applies every commit where it arrives, which shows the
//new content a frame early rather than dropping it - wrong, but wrong in the
//direction that keeps a client running. gtk sets sync on its subsurfaces and
//firefox sets desync on the one it draws into
static void subsurface_set_sync(WClient *client, WResource *resource) {

  Task *child = wl_resource_get_user_data(resource);
  if (child)
    child->subsurface_synchronized = true;
}

static void subsurface_set_desync(WClient *client, WResource *resource) {

  Task *child = wl_resource_get_user_data(resource);
  if (child)
    child->subsurface_synchronized = false;
}

static const struct wl_subsurface_interface subsurface_implementation = {
    .destroy = subsurface_destroy,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

//the role goes away and the surface stays. it is unmapped by losing its
//parent, exactly as the protocol says
static void destroy_subsurface_resource(WResource *resource) {

  Task *child = wl_resource_get_user_data(resource);

  //the wl_surface went first and cleared this - see forget_subsurface_role()
  if (!child)
    return;

  child->subsurface_resource = NULL;

  if (child->parent)
    orphan_subsurface(child);

  log_info("Destroyed subsurface");
}

//libwayland destroys a client's resources in creation order, so the wl_surface
//can go before the wl_subsurface made out of it. the Task is freed either way,
//and the handlers above read it straight out of the user data
void forget_subsurface_role(Task *task) {

  if (task->subsurface_resource) {
    wl_resource_set_user_data(task->subsurface_resource, NULL);
    task->subsurface_resource = NULL;
  }
}

//would making parent a child of child close a loop
static bool is_descendant(Task *parent, Task *child) {

  for (Task *task = parent; task; task = task->parent)
    if (task == child)
      return true;

  return false;
}

static void get_subsurface(WClient *client, WResource *resource, uint32_t id,
                           WResource *surface_resource,
                           WResource *parent_resource) {

  Task *child = wl_resource_get_user_data(surface_resource);
  Task *parent = wl_resource_get_user_data(parent_resource);

  if (!child || !parent) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface is gone");
    return;
  }

  if (child == parent || is_descendant(parent, child)) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT,
                           "a surface cannot be its own ancestor");
    return;
  }

  //one role per surface, and a window is already playing one
  if (child->subsurface_resource || child->top_level) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface already has a role");
    return;
  }

  WResource *subsurface = wl_resource_create(
      client, &wl_subsurface_interface, wl_resource_get_version(resource), id);

  if (!subsurface) {
    wl_client_post_no_memory(client);
    log_error("Can't create subsurface resource");
    return;
  }

  wl_resource_set_implementation(subsurface, &subsurface_implementation, child,
                                 destroy_subsurface_resource);

  child->subsurface_resource = subsurface;
  child->parent = parent;

  //synchronized is what a subsurface starts out as, per the protocol
  child->subsurface_synchronized = true;

  //at the tail, which is the top of the stack
  wl_list_remove(&child->parent_link);
  wl_list_insert(parent->children.prev, &child->parent_link);

  log_info("New subsurface with ID %u", id);
}

static void subcompositor_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_subcompositor_interface subcompositor_implementation = {
    .destroy = subcompositor_destroy,
    .get_subsurface = get_subsurface,
};

static void bind_subcompositor(WClient *client, void *data, uint32_t version,
                               uint32_t id) {

  WResource *resource =
      wl_resource_create(client, &wl_subcompositor_interface, version, id);

  if (!resource) {
    wl_client_post_no_memory(client);
    log_error("Can't create subcompositor resource");
    return;
  }

  wl_resource_set_implementation(resource, &subcompositor_implementation, data,
                                 NULL);

  log_info("Subcompositor bound");
}

void init_subcompositor(void) {

  //firefox does not degrade without this one, it aborts:
  //MOZ_RELEASE_ASSERT(GetSubcompositor()). the interface has only ever had
  //one version
  wl_global_create(compositor.display, &wl_subcompositor_interface, 1,
                   &compositor, bind_subcompositor);

  log_info("Added subcompositor global");
}
