#include "popup.h"

#include "desktop-server.h"

#include <stdio.h>
#include <wayland-server.h>
#include "log.h"

//a positioner is client-side bookkeeping until a popup uses it, and sword
//never gets that far. the requests still need handlers: libwayland dispatches
//a NULL entry as a call and takes the compositor down with the client
static void positioner_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static void positioner_set_size(WClient *client, WResource *resource,
                                int32_t width, int32_t height) {}

static void positioner_set_anchor_rect(WClient *client, WResource *resource,
                                       int32_t x, int32_t y, int32_t width,
                                       int32_t height) {}

static void positioner_set_anchor(WClient *client, WResource *resource,
                                  uint32_t anchor) {}

static void positioner_set_gravity(WClient *client, WResource *resource,
                                   uint32_t gravity) {}

static void positioner_set_constraint_adjustment(WClient *client,
                                                 WResource *resource,
                                                 uint32_t adjustment) {}

static void positioner_set_offset(WClient *client, WResource *resource,
                                  int32_t x, int32_t y) {}

//since version 3
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

void create_positioner(WClient *client, WResource *resource, uint32_t id) {

  WResource *positioner = wl_resource_create(
      client, &xdg_positioner_interface, wl_resource_get_version(resource), id);

  if (!positioner) {
    wl_client_post_no_memory(client);
    log_error("Can't create positioner");
    return;
  }

  wl_resource_set_implementation(positioner, &positioner_implementation, NULL,
                                 NULL);

  log_info("Created positioner");
}

static void popup_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

//there is no pointer, so nothing can hold or break a grab
static void popup_grab(WClient *client, WResource *resource, WResource *seat,
                       uint32_t serial) {}

//since version 3
static void popup_reposition(WClient *client, WResource *resource,
                             WResource *positioner, uint32_t token) {}

static const struct xdg_popup_interface popup_implementation = {
    .destroy = popup_destroy,
    .grab = popup_grab,
    .reposition = popup_reposition,
};

void create_popup(WClient *client, WResource *resource, uint32_t id,
                  WResource *parent, WResource *positioner) {

  WResource *popup = wl_resource_create(client, &xdg_popup_interface,
                                        wl_resource_get_version(resource), id);

  if (!popup) {
    wl_client_post_no_memory(client);
    log_error("Can't create popup");
    return;
  }

  wl_resource_set_implementation(popup, &popup_implementation, NULL, NULL);

  //the popup is dismissed the moment it is asked for. a client told popup_done
  //tears the menu down and carries on, which is the only one of the three
  //options that leaves it alive: refusing with a protocol error kills it, and
  //never answering leaves a client that took a grab waiting forever
  xdg_popup_send_popup_done(popup);

  log_info("Popup dismissed, sword has nowhere to put it");
}
