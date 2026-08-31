#include "primary_selection.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "primary-selection.h"
#include "compositor.h"
#include "input.h"
#include "log.h"

//the X-style middle-click selection, kept as its own singleton the same shape
//as wl_data_device_manager's - see data_device.c, which this is a near-copy
//of with the drag half removed: there was never a drag half here to begin
//with, the protocol has no set_actions/start_drag at all
typedef struct PrimarySource {
  WResource *resource;
  char **mime_types;
  int mime_type_count;
  int mime_type_capacity;
} PrimarySource;

typedef struct PrimaryOffer {
  WResource *resource;
  WResource *source;
  struct wl_listener source_destroy;
} PrimaryOffer;

//the source that owns the primary selection right now, or NULL for empty
static WResource *primary_selection_source;

//every zwp_primary_selection_device_v1 a client has made
static struct wl_list primary_devices;

static void send_primary_selection_to_device(WResource *device);

// -------------------------------------------------------------------- source

static void primary_source_add_mime_type(PrimarySource *source,
                                         const char *mime_type) {

  if (source->mime_type_count == source->mime_type_capacity) {
    int capacity =
        source->mime_type_capacity ? source->mime_type_capacity * 2 : 8;

    char **mime_types = realloc(source->mime_types, capacity * sizeof(char *));
    if (!mime_types)
      return;

    source->mime_types = mime_types;
    source->mime_type_capacity = capacity;
  }

  source->mime_types[source->mime_type_count++] = strdup(mime_type);
}

static void primary_source_offer(WClient *client, WResource *resource,
                                 const char *mime_type) {
  PrimarySource *source = wl_resource_get_user_data(resource);
  primary_source_add_mime_type(source, mime_type);
}

static void primary_source_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_source_v1_interface
    primary_source_interface = {.offer = primary_source_offer,
                                .destroy = primary_source_destroy};

static void clear_primary_selection(void) {

  primary_selection_source = NULL;

  WResource *device;
  wl_resource_for_each(device, &primary_devices)
      zwp_primary_selection_device_v1_send_selection(device, NULL);
}

static void destroy_primary_source(WResource *resource) {
  PrimarySource *source = wl_resource_get_user_data(resource);

  if (primary_selection_source == resource)
    clear_primary_selection();

  for (int i = 0; i < source->mime_type_count; i++)
    free(source->mime_types[i]);

  free(source->mime_types);
  free(source);

  log_info("Destroyed primary selection source");
}

static void create_primary_source(WClient *client, WResource *resource,
                                  uint32_t id) {

  PrimarySource *source = calloc(1, sizeof(PrimarySource));
  if (!source) {
    wl_client_post_no_memory(client);
    return;
  }

  source->resource = wl_resource_create(
      client, &zwp_primary_selection_source_v1_interface,
      wl_resource_get_version(resource), id);

  if (!source->resource) {
    free(source);
    wl_client_post_no_memory(client);
    log_error("Can't create primary selection source resource");
    return;
  }

  wl_resource_set_implementation(source->resource, &primary_source_interface,
                                 source, destroy_primary_source);

  log_info("Created primary selection source");
}

// --------------------------------------------------------------------- offer

static void handle_primary_source_destroyed(struct wl_listener *listener,
                                            void *data) {
  PrimaryOffer *offer = wl_container_of(listener, offer, source_destroy);

  offer->source = NULL;

  wl_list_remove(&offer->source_destroy.link);
  wl_list_init(&offer->source_destroy.link);
}

static void primary_offer_receive(WClient *client, WResource *resource,
                                  const char *mime_type, int32_t fd) {

  PrimaryOffer *offer = wl_resource_get_user_data(resource);

  if (offer->source)
    zwp_primary_selection_source_v1_send_send(offer->source, mime_type, fd);

  close(fd);
}

static void primary_offer_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_offer_v1_interface
    primary_offer_interface = {.receive = primary_offer_receive,
                               .destroy = primary_offer_destroy};

static void destroy_primary_offer(WResource *resource) {
  PrimaryOffer *offer = wl_resource_get_user_data(resource);

  wl_list_remove(&offer->source_destroy.link);
  free(offer);
}

// -------------------------------------------------------------------- device

static void send_primary_selection_to_device(WResource *device) {

  if (!primary_selection_source) {
    zwp_primary_selection_device_v1_send_selection(device, NULL);
    return;
  }

  WClient *client = wl_resource_get_client(device);
  PrimarySource *source = wl_resource_get_user_data(primary_selection_source);

  PrimaryOffer *offer = calloc(1, sizeof(PrimaryOffer));
  if (!offer) {
    wl_client_post_no_memory(client);
    return;
  }

  offer->resource = wl_resource_create(
      client, &zwp_primary_selection_offer_v1_interface,
      wl_resource_get_version(device), 0);
  if (!offer->resource) {
    free(offer);
    wl_client_post_no_memory(client);
    return;
  }

  offer->source = primary_selection_source;
  offer->source_destroy.notify = handle_primary_source_destroyed;
  wl_resource_add_destroy_listener(primary_selection_source,
                                   &offer->source_destroy);

  wl_resource_set_implementation(offer->resource, &primary_offer_interface,
                                 offer, destroy_primary_offer);

  zwp_primary_selection_device_v1_send_data_offer(device, offer->resource);

  for (int i = 0; i < source->mime_type_count; i++)
    zwp_primary_selection_offer_v1_send_offer(offer->resource,
                                              source->mime_types[i]);

  zwp_primary_selection_device_v1_send_selection(device, offer->resource);
}

void primary_selection_offer_selection(WClient *client) {

  WResource *device;
  wl_resource_for_each(device, &primary_devices) {
    if (wl_resource_get_client(device) == client)
      send_primary_selection_to_device(device);
  }
}

void primary_selection_clear_selection(WClient *client) {

  WResource *device;
  wl_resource_for_each(device, &primary_devices) {
    if (wl_resource_get_client(device) == client)
      zwp_primary_selection_device_v1_send_selection(device, NULL);
  }
}

static void set_primary_selection(WClient *client, WResource *resource,
                                  WResource *source, uint32_t serial) {

  //same call sword does not make on the wl_data_device side either - see
  //data_device.c's set_selection()

  if (primary_selection_source == source)
    return;

  if (primary_selection_source)
    zwp_primary_selection_source_v1_send_cancelled(primary_selection_source);

  primary_selection_source = source;

  WClient *focused = keyboard_focus_client();
  if (focused)
    primary_selection_offer_selection(focused);

  log_info("Primary selection %s", source ? "set" : "cleared");
}

static void destroy_primary_device(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_device_v1_interface
    primary_device_interface = {.set_selection = set_primary_selection,
                                .destroy = destroy_primary_device};

static void handle_primary_device_destroyed(WResource *resource) {
  wl_list_remove(wl_resource_get_link(resource));
  log_info("Destroyed primary selection device");
}

static void get_primary_device(WClient *client, WResource *resource,
                               uint32_t id, WResource *seat) {

  WResource *device = wl_resource_create(
      client, &zwp_primary_selection_device_v1_interface,
      wl_resource_get_version(resource), id);

  if (!device) {
    wl_client_post_no_memory(client);
    log_error("Can't create primary selection device resource");
    return;
  }

  wl_resource_set_implementation(device, &primary_device_interface,
                                 &compositor, handle_primary_device_destroyed);

  wl_list_insert(&primary_devices, wl_resource_get_link(device));

  if (keyboard_focus_client() == client)
    send_primary_selection_to_device(device);

  log_info("Got primary selection device");
}

// ------------------------------------------------------------------ manager

static void destroy_primary_manager(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct zwp_primary_selection_device_manager_v1_interface
    primary_manager_interface = {.create_source = create_primary_source,
                                 .get_device = get_primary_device,
                                 .destroy = destroy_primary_manager};

static void bind_primary_selection_manager(WClient *client, void *data,
                                           uint32_t version, uint32_t id) {

  WResource *resource = wl_resource_create(
      client, &zwp_primary_selection_device_manager_v1_interface, version,
      id);

  if (!resource) {
    wl_client_post_no_memory(client);
    log_error("Can't create primary selection device manager resource");
    return;
  }

  wl_resource_set_implementation(resource, &primary_manager_interface,
                                 &compositor, NULL);

  log_info("Primary selection device manager bound");
}

void init_primary_selection() {

  wl_list_init(&primary_devices);

  wl_global_create(compositor.display,
                   &zwp_primary_selection_device_manager_v1_interface,
                   PRIMARY_SELECTION_MANAGER_VERSION, &compositor,
                   bind_primary_selection_manager);
}
