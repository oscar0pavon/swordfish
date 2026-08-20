#include "output.h"

#include <stdint.h>
#include <stdio.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "compositor.h"
#include "window.h"

//swordfish renders one image of a fixed size into one window, so there is one
//output and its mode is that image. WINDOW_WIDTH/WINDOW_HEIGHT stays the app's
//authority on the size, the same as it is for the swap chain and the camera -
//once resizing exists, a new mode goes out to everyone in output_resources
#define OUTPUT_REFRESH_MHZ 60000

//a physical size of zero is what the protocol says to send for an unknown one,
//and a client that divides by it to get a dpi gets an infinity instead. the
//panel is described as an ordinary 96 dpi one so the arithmetic lands somewhere
#define OUTPUT_DPI 96
#define OUTPUT_MILLIMETRES(pixels) ((int32_t)((pixels) * 25.4 / OUTPUT_DPI))

//every wl_output resource bound right now. a client can bind the same global
//more than once, and wl_surface.enter is owed on each of its own resources
static struct wl_list output_resources;

//wl_output.release, since version 3. without an implementation on the resource
//libwayland dispatches the request through a NULL table
static void output_release(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_output_interface output_interface = {
    .release = output_release};

static void destroy_output(WResource *resource) {
  wl_list_remove(wl_resource_get_link(resource));
  printf("Destroyed output\n");
}

//everything the client needs to describe the output, in the order the protocol
//asks for it: the properties first, then done to say the burst is over
static void send_output_state(WResource *resource) {

  uint32_t version = wl_resource_get_version(resource);

  wl_output_send_geometry(resource, 0, 0, OUTPUT_MILLIMETRES(WINDOW_WIDTH),
                          OUTPUT_MILLIMETRES(WINDOW_HEIGHT),
                          WL_OUTPUT_SUBPIXEL_UNKNOWN, "swordfish", "swordfish",
                          WL_OUTPUT_TRANSFORM_NORMAL);

  //current and preferred are the same flag set here: there is only one mode and
  //nothing can switch away from it
  wl_output_send_mode(resource,
                      WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                      WINDOW_WIDTH, WINDOW_HEIGHT, OUTPUT_REFRESH_MHZ);

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
    wl_output_send_scale(resource, 1);

  if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
    wl_output_send_name(resource, "swordfish-0");

  if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
    wl_output_send_description(resource, "Swordfish scene");

  //nothing above takes effect until this arrives - a client applies the whole
  //burst at once, so leaving it out leaves the client with no output at all
  if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
    wl_output_send_done(resource);
}

static void bind_output(WClient *client, void *data, uint32_t version,
                        uint32_t id) {

  WResource *resource =
      wl_resource_create(client, &wl_output_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't create output resource\n");
    return;
  }

  wl_resource_set_implementation(resource, &output_interface, &compositor,
                                 destroy_output);

  wl_list_insert(&output_resources, wl_resource_get_link(resource));

  send_output_state(resource);

  printf("Output bound\n");
}

void output_send_surface_enter(WResource *surface_resource) {

  WClient *client = wl_resource_get_client(surface_resource);

  WResource *output;
  wl_resource_for_each(output, &output_resources) {
    if (wl_resource_get_client(output) == client)
      wl_surface_send_enter(surface_resource, output);
  }
}

void init_output() {

  wl_list_init(&output_resources);

  wl_global_create(compositor.display, &wl_output_interface, OUTPUT_VERSION,
                   &compositor, bind_output);
}
