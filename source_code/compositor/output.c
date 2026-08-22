#include "output.h"

#include <stdint.h>
#include <stdio.h>
#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "compositor.h"
#include "outputs.h"

//swordfish advertises one wl_output global per SwordfishOutput, each one's
//mode fixed to that output's own render target size - see outputs.h
#define OUTPUT_REFRESH_MHZ 60000

//a physical size of zero is what the protocol says to send for an unknown one,
//and a client that divides by it to get a dpi gets an infinity instead. the
//panel is described as an ordinary 96 dpi one so the arithmetic lands somewhere
#define OUTPUT_DPI 96
#define OUTPUT_MILLIMETRES(pixels) ((int32_t)((pixels) * 25.4 / OUTPUT_DPI))

//every wl_output resource bound right now, across every output - a client can
//bind more than one global and more than once each, and wl_surface.enter is
//owed on each of its own resources for the output its surface is on. which
//output a resource belongs to is its wl_resource user data, a SwordfishOutput*
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
static void send_output_state(WResource *resource, SwordfishOutput *out) {

  uint32_t version = wl_resource_get_version(resource);

  wl_output_send_geometry(resource, out->x, out->y,
                          OUTPUT_MILLIMETRES(out->width),
                          OUTPUT_MILLIMETRES(out->height),
                          WL_OUTPUT_SUBPIXEL_UNKNOWN, "swordfish", "swordfish",
                          WL_OUTPUT_TRANSFORM_NORMAL);

  //current and preferred are the same flag set here: there is only one mode and
  //nothing can switch away from it
  wl_output_send_mode(resource,
                      WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
                      out->width, out->height, OUTPUT_REFRESH_MHZ);

  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
    wl_output_send_scale(resource, 1);

  if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
    wl_output_send_name(resource, out->name);

  if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
    wl_output_send_description(resource, "Swordfish scene");

  //nothing above takes effect until this arrives - a client applies the whole
  //burst at once, so leaving it out leaves the client with no output at all
  if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
    wl_output_send_done(resource);
}

static void bind_output(WClient *client, void *data, uint32_t version,
                        uint32_t id) {

  SwordfishOutput *out = data;

  WResource *resource =
      wl_resource_create(client, &wl_output_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't create output resource\n");
    return;
  }

  wl_resource_set_implementation(resource, &output_interface, out,
                                 destroy_output);

  wl_list_insert(&output_resources, wl_resource_get_link(resource));

  send_output_state(resource, out);

  printf("Output %s bound\n", out->name);
}

void output_send_surface_enter(WResource *surface_resource,
                               int output_index) {

  WClient *client = wl_resource_get_client(surface_resource);
  SwordfishOutput *out = &swordfish_outputs[output_index];

  WResource *output;
  wl_resource_for_each(output, &output_resources) {
    if (wl_resource_get_client(output) == client &&
        (SwordfishOutput *)wl_resource_get_user_data(output) == out)
      wl_surface_send_enter(surface_resource, output);
  }
}

void init_output() {

  wl_list_init(&output_resources);

  //one global per output - a client that only cares about one monitor still
  //has to see all of them to place its windows correctly
  for (int i = 0; i < swordfish_outputs_count; i++)
    wl_global_create(compositor.display, &wl_output_interface, OUTPUT_VERSION,
                     &swordfish_outputs[i], bind_output);
}
