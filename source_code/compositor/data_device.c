#include "data_device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "compositor.h"
#include "input.h"

//the clipboard. the compositor never sees the data itself: it keeps whichever
//wl_data_source last claimed the selection, tells the focused client what mime
//types are on offer, and when that client asks to read one it hands the pipe
//straight to the source client, which does the writing
typedef struct DataSource {
  WResource *resource;
  //the mime types the client offered, in the order it offered them. a toolkit
  //copying rich text offers a dozen or more, so this grows rather than caps
  char **mime_types;
  int mime_type_count;
  int mime_type_capacity;
} DataSource;

//one client's view of the selection. it names a source that belongs to another
//client, and that client is free to destroy it first - the same shape as a
//wl_buffer outliving the surface that attached it, and handled the same way
typedef struct DataOffer {
  WResource *resource;
  WResource *source;
  struct wl_listener source_destroy;
} DataOffer;

//the source that owns the selection right now, or NULL for an empty clipboard
static WResource *selection_source;

//every wl_data_device a client has made. the selection goes to whichever of
//them has the keyboard, so they have to be findable by client
static struct wl_list data_devices;

static void send_selection_to_device(WResource *device);

// ---------------------------------------------------------------- data source

static void data_source_add_mime_type(DataSource *source,
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

static void data_source_offer(WClient *client, WResource *resource,
                              const char *mime_type) {
  DataSource *source = wl_resource_get_user_data(resource);
  data_source_add_mime_type(source, mime_type);
}

static void data_source_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

//drag-and-drop actions, since version 3. there is no drag, so there is nothing
//to negotiate - but the request has to be answered or the client is dispatched
//through a NULL table
static void data_source_set_actions(WClient *client, WResource *resource,
                                    uint32_t dnd_actions) {}

static const struct wl_data_source_interface data_source_interface = {
    .offer = data_source_offer,
    .destroy = data_source_destroy,
    .set_actions = data_source_set_actions};

//the client that owned the clipboard has gone away, so the clipboard is empty.
//every client holding a device has to be told, or it goes on believing there is
//something to paste and asks a freed resource for it
static void clear_selection(void) {

  selection_source = NULL;

  WResource *device;
  wl_resource_for_each(device, &data_devices)
      wl_data_device_send_selection(device, NULL);
}

static void destroy_data_source(WResource *resource) {
  DataSource *source = wl_resource_get_user_data(resource);

  if (selection_source == resource)
    clear_selection();

  for (int i = 0; i < source->mime_type_count; i++)
    free(source->mime_types[i]);

  free(source->mime_types);
  free(source);

  printf("Destroyed data source\n");
}

static void create_data_source(WClient *client, WResource *resource,
                               uint32_t id) {

  DataSource *source = calloc(1, sizeof(DataSource));
  if (!source) {
    wl_client_post_no_memory(client);
    return;
  }

  source->resource = wl_resource_create(
      client, &wl_data_source_interface, wl_resource_get_version(resource), id);

  if (!source->resource) {
    free(source);
    wl_client_post_no_memory(client);
    printf("Can't create data source resource\n");
    return;
  }

  wl_resource_set_implementation(source->resource, &data_source_interface,
                                 source, destroy_data_source);

  printf("Created data source\n");
}

// ----------------------------------------------------------------- data offer

//the source is the other client's and can go at any moment. the offer outlives
//it, so the pointer is dropped here rather than left dangling for a receive to
//write through
static void handle_source_destroyed(struct wl_listener *listener, void *data) {
  DataOffer *offer = wl_container_of(listener, offer, source_destroy);

  offer->source = NULL;

  //re-initialised so the offer's own destructor can remove it again without
  //walking a list this link is no longer in
  wl_list_remove(&offer->source_destroy.link);
  wl_list_init(&offer->source_destroy.link);
}

//the paste. the fd is the reading client's end of its own pipe, and it is the
//source client that writes into it - the data never passes through here
static void data_offer_receive(WClient *client, WResource *resource,
                               const char *mime_type, int32_t fd) {

  DataOffer *offer = wl_resource_get_user_data(resource);

  if (offer->source)
    wl_data_source_send_send(offer->source, mime_type, fd);

  //libwayland dups the fd into the source's connection, so this one is ours to
  //close whether or not it was forwarded. leaving it open holds the pipe's
  //write end and the reader waits on an EOF that never comes
  close(fd);
}

//drag-and-drop only, and there is no drag. accept and set_actions are what a
//destination answers a wl_data_device.enter with, which is never sent
static void data_offer_accept(WClient *client, WResource *resource,
                              uint32_t serial, const char *mime_type) {}

static void data_offer_set_actions(WClient *client, WResource *resource,
                                   uint32_t dnd_actions,
                                   uint32_t preferred_action) {}

//the protocol says finish on a selection offer is an invalid_finish error, but
//killing a client over a request it cannot have meant is worse than ignoring it
static void data_offer_finish(WClient *client, WResource *resource) {}

static void data_offer_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_data_offer_interface data_offer_interface = {
    .accept = data_offer_accept,
    .receive = data_offer_receive,
    .destroy = data_offer_destroy,
    .finish = data_offer_finish,
    .set_actions = data_offer_set_actions};

static void destroy_data_offer(WResource *resource) {
  DataOffer *offer = wl_resource_get_user_data(resource);

  wl_list_remove(&offer->source_destroy.link);
  free(offer);
}

// ---------------------------------------------------------------- data device

//wl_data_device.data_offer, then one offer event per mime type, then
//wl_data_device.selection naming it. the client applies the burst as a unit,
//exactly like the output's
static void send_selection_to_device(WResource *device) {

  if (!selection_source) {
    wl_data_device_send_selection(device, NULL);
    return;
  }

  WClient *client = wl_resource_get_client(device);
  DataSource *source = wl_resource_get_user_data(selection_source);

  DataOffer *offer = calloc(1, sizeof(DataOffer));
  if (!offer) {
    wl_client_post_no_memory(client);
    return;
  }

  //id 0 asks libwayland for an id out of the server's own range: this object is
  //the compositor's to create, not the client's
  offer->resource = wl_resource_create(client, &wl_data_offer_interface,
                                       wl_resource_get_version(device), 0);
  if (!offer->resource) {
    free(offer);
    wl_client_post_no_memory(client);
    return;
  }

  offer->source = selection_source;
  offer->source_destroy.notify = handle_source_destroyed;
  wl_resource_add_destroy_listener(selection_source, &offer->source_destroy);

  wl_resource_set_implementation(offer->resource, &data_offer_interface, offer,
                                 destroy_data_offer);

  wl_data_device_send_data_offer(device, offer->resource);

  for (int i = 0; i < source->mime_type_count; i++)
    wl_data_offer_send_offer(offer->resource, source->mime_types[i]);

  wl_data_device_send_selection(device, offer->resource);
}

void data_device_offer_selection(WClient *client) {

  WResource *device;
  wl_resource_for_each(device, &data_devices) {
    if (wl_resource_get_client(device) == client)
      send_selection_to_device(device);
  }
}

static void set_selection(WClient *client, WResource *resource,
                          WResource *source, uint32_t serial) {

  //the serial should be checked against the input event the client is claiming
  //the clipboard on behalf of. swordfish does not keep its serials around long
  //enough to tell, and refusing on a serial it cannot verify would mean no
  //clipboard at all

  if (selection_source == source)
    return;

  //the previous owner is told the moment it stops being the owner. a client
  //that is never cancelled goes on holding the data it copied forever
  if (selection_source)
    wl_data_source_send_cancelled(selection_source);

  selection_source = source;

  //the client that copied is usually the focused one, and it is entitled to
  //read its own clipboard back. everyone else finds out when they take the
  //keyboard, in data_device_offer_selection()
  WClient *focused = keyboard_focus_client();
  if (focused)
    data_device_offer_selection(focused);

  printf("Selection %s\n", source ? "set" : "cleared");
}

//there is no drag: no second quad to carry an icon, no grab to hold, and no
//wl_data_device.enter to send anyone. cancelled is the only answer that leaves
//the source client alive and unstuck - the same choice popup.c makes with
//xdg_popup.popup_done. silence would hang a client waiting on a drop
static void start_drag(WClient *client, WResource *resource, WResource *source,
                       WResource *origin, WResource *icon, uint32_t serial) {

  if (source)
    wl_data_source_send_cancelled(source);
}

//wl_data_device.release, since version 2
static void release_data_device(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_data_device_interface data_device_interface = {
    .start_drag = start_drag,
    .set_selection = set_selection,
    .release = release_data_device};

static void destroy_data_device(WResource *resource) {
  wl_list_remove(wl_resource_get_link(resource));
  printf("Destroyed data device\n");
}

static void get_data_device(WClient *client, WResource *resource, uint32_t id,
                            WResource *seat) {

  WResource *device = wl_resource_create(
      client, &wl_data_device_interface, wl_resource_get_version(resource), id);

  if (!device) {
    wl_client_post_no_memory(client);
    printf("Can't create data device resource\n");
    return;
  }

  wl_resource_set_implementation(device, &data_device_interface, &compositor,
                                 destroy_data_device);

  wl_list_insert(&data_devices, wl_resource_get_link(device));

  //a client that asks for a device while something is already on the clipboard
  //has to be told about it, or it only learns of a copy that happens after this
  if (keyboard_focus_client() == client)
    send_selection_to_device(device);

  printf("Got data device\n");
}

// --------------------------------------------------------------- the manager

static const struct wl_data_device_manager_interface
    data_device_manager_interface = {.create_data_source = create_data_source,
                                     .get_data_device = get_data_device};

static void bind_data_device_manager(WClient *client, void *data,
                                     uint32_t version, uint32_t id) {

  WResource *resource = wl_resource_create(
      client, &wl_data_device_manager_interface, version, id);

  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't create data device manager resource\n");
    return;
  }

  wl_resource_set_implementation(resource, &data_device_manager_interface,
                                 &compositor, NULL);

  printf("Data device manager bound\n");
}

void init_data_device() {

  wl_list_init(&data_devices);

  wl_global_create(compositor.display, &wl_data_device_manager_interface,
                   DATA_DEVICE_MANAGER_VERSION, &compositor,
                   bind_data_device_manager);
}
