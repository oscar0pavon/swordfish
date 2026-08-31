#include "subcompositor.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include "compositor.h"
#include "log.h"
#include "surface.h"
#include "top_level.h"

bool task_is_child(Task *task) { return task->parent != NULL; }

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

//the part of the buffer that is the window proper. a client that draws its
//own decorations puts its shadow *outside* that - firefox pads 20 pixels on
//all four sides of it, which is why its buffer comes out 1120x1960 in a
//1080x1920 cell - and xdg_surface.set_window_geometry is the only thing that
//says where the window inside the buffer begins. without it the shadow is
//scaled into the cell along with the window and every pixel of the client
//lands between two of the output's: a 1.037x minification of the whole
//window, which is a permanently blurry firefox.
//
//the rect is the client's to send and may arrive before it has attached
//anything, so one that does not fit inside the buffer it has is refused in
//favour of the whole buffer
static void task_window_geometry(Task *task, double surface_width,
                                 double surface_height, double *x, double *y,
                                 double *width, double *height) {

  *x = 0;
  *y = 0;
  *width = surface_width;
  *height = surface_height;

  if (!task->top_level || !task->top_level->surface)
    return;

  DesktopSurface *desktop_surface = task->top_level->surface;

  if (desktop_surface->geometry_width <= 0 ||
      desktop_surface->geometry_height <= 0)
    return;

  if (desktop_surface->geometry_x < 0 || desktop_surface->geometry_y < 0)
    return;

  if (desktop_surface->geometry_x + desktop_surface->geometry_width >
          surface_width ||
      desktop_surface->geometry_y + desktop_surface->geometry_height >
          surface_height)
    return;

  *x = desktop_surface->geometry_x;
  *y = desktop_surface->geometry_y;
  *width = desktop_surface->geometry_width;
  *height = desktop_surface->geometry_height;
}

//where a surface's own origin lands on the virtual desktop, and how much its
//coordinates are stretched on the way there.
//
//deliberately separate from the surface's *size*, because a surface with no
//buffer still has a position and things hang off it. firefox's menus are
//exactly that: the popup's own wl_surface carries no pixels and the menu is
//drawn by a subsurface inside it. a rectangle that could not be worked out
//without a buffer made that whole subtree unpositionable, and the menu never
//appeared at all
static bool task_origin_and_scale(Task *task, double *x, double *y,
                                  double *scale_x, double *scale_y) {

  if (!task_is_child(task)) {

    //a window is drawn in the cell the layout gave it, and whichever scale
    //that works out to is what everything inside it inherits
    if (task->tile_width > 0) {

      double surface_width, surface_height;
      task_surface_size(task, task->tile_width, task->tile_height,
                        &surface_width, &surface_height);

      //the cell was configured as a *window geometry* size, so it is the
      //geometry rect that has to land on it and not the buffer around it
      double geometry_x, geometry_y, geometry_width, geometry_height;
      task_window_geometry(task, surface_width, surface_height, &geometry_x,
                           &geometry_y, &geometry_width, &geometry_height);

      //a window that already fits the cell is drawn at its own size rather
      //than stretched up to fill it. the stretch is only there to cover the
      //frames between a configure and the client repainting, and in the
      //direction where the cell grew - close a window and the survivor is
      //given the whole output - that cover is a 2x upscale of the old buffer:
      //four frames of a visibly blurred window, which is worse than four
      //frames of an unpainted band beside a crisp one. the other direction
      //still stretches, both because minifying for two frames does not read
      //as broken and because a buffer larger than its cell drawn at its own
      //size would spill over the window next to it - there is no scissor here
      //to stop it
      if (geometry_width <= task->tile_width &&
          geometry_height <= task->tile_height) {
        *scale_x = 1;
        *scale_y = 1;
      } else {
        *scale_x = task->tile_width / geometry_width;
        *scale_y = task->tile_height / geometry_height;
      }

      //what is returned is still the origin of the *buffer*, since that is
      //what the quad is drawn from and what a subsurface's own offset is
      //measured in - it is just moved back by the shadow, so that the window
      //inside the buffer is what covers the cell
      *x = task->tile_x - geometry_x * *scale_x;
      *y = task->tile_y - geometry_y * *scale_y;

      return true;
    }

    //no cell: drawn where it is, at its own size
    *x = 0;
    *y = 0;
    *scale_x = 1;
    *scale_y = 1;
    return true;
  }

  double parent_x, parent_y;

  if (!task_origin_and_scale(task->parent, &parent_x, &parent_y, scale_x,
                             scale_y))
    return false;

  //the child is placed in the parent's surface coordinates, so its offset is
  //stretched the same way the parent's own pixels are - and it goes on
  //carrying that scale to its own children. this is the ratio pointer_inside()
  //divides the cursor back through
  *x = parent_x + task->child_x * *scale_x;
  *y = parent_y + task->child_y * *scale_y;

  return true;
}

bool task_screen_rect(Task *task, double *x, double *y, double *width,
                      double *height) {

  double scale_x, scale_y;

  if (!task_origin_and_scale(task, x, y, &scale_x, &scale_y))
    return false;

  //a surface with no buffer has a position and no extent, and both callers
  //read that correctly: nothing is drawn for it, and the cursor is inside
  //nothing
  if (!task->image) {
    *width = 0;
    *height = 0;
    return true;
  }

  *width = task->image->width * scale_x;
  *height = task->image->heigth * scale_y;

  return true;
}

//what the scene actually looks like from the compositor's side: every surface,
//what role it is playing, what it hangs off and where that puts it. a window
//made of a tree of surfaces has no other way of being read - the client's own
//protocol log says what it asked for, not what sword made of it
//frames to wait before dumping the tree again, so the picture is the one at
//drawing time rather than the one in the middle of the request that changed it
int surface_tree_dump_countdown;

void log_surface_tree(const char *why) {

  log_info("--- surface tree (%s) ---", why);

  Task *task;

  wl_list_for_each_reverse(task, &compositor.surfaces, link) {

    const char *role = "none";

    if (task->top_level)
      role = "window";
    else if (task->popup_resource)
      role = "popup";
    else if (task->subsurface_resource)
      role = "subsurface";
    else if (task->is_cursor)
      role = "cursor";

    double x = 0, y = 0, width = 0, height = 0;
    bool placed = task_screen_rect(task, &x, &y, &width, &height);

    log_info("  task %p %-10s parent %p at %+i%+i buffer %s draw %i "
             "children %i -> screen %.0f %.0f %.0fx%.0f%s",
             (void *)task, role, (void *)task->parent, task->child_x,
             task->child_y, task->image ? "yes" : "no", task->can_draw,
             wl_list_length(&task->children), x, y, width, height,
             placed ? "" : " (no position)");
  }
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

  child->child_x = x;
  child->child_y = y;

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
