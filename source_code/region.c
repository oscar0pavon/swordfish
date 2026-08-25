#include "region.h"

#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "log.h"

//a region starts out empty, and an empty one contains nothing. that is not a
//degenerate case to guard against - it is exactly what firefox sets on the
//subsurface it renders into, to say the pointer belongs to the window behind it
bool region_contains(Region *region, double x, double y) {

  bool inside = false;

  //replayed in order: an add puts this point in, a subtract after it takes it
  //back out. for one point that is the whole of region arithmetic
  for (int i = 0; i < region->count; i++) {

    RegionOperation *operation = &region->operations[i];

    if (x < operation->x || y < operation->y)
      continue;

    if (x >= operation->x + operation->width ||
        y >= operation->y + operation->height)
      continue;

    inside = !operation->subtract;
  }

  return inside;
}

static void region_add_operation(Region *region, int32_t x, int32_t y,
                                 int32_t width, int32_t height,
                                 bool subtract) {

  if (region->count == region->capacity) {

    int capacity = region->capacity ? region->capacity * 2 : 8;

    RegionOperation *operations =
        realloc(region->operations, capacity * sizeof(RegionOperation));

    if (!operations) {
      log_error("Can't grow region to %i operations", capacity);
      return;
    }

    region->operations = operations;
    region->capacity = capacity;
  }

  region->operations[region->count++] = (RegionOperation){
      .x = x, .y = y, .width = width, .height = height, .subtract = subtract};
}

void region_copy(Region *destination, Region *source) {

  region_clean(destination);

  if (!source || source->count == 0)
    return;

  destination->operations =
      malloc(source->count * sizeof(RegionOperation));

  if (!destination->operations) {
    log_error("Can't copy a region of %i operations", source->count);
    return;
  }

  memcpy(destination->operations, source->operations,
         source->count * sizeof(RegionOperation));

  destination->count = source->count;
  destination->capacity = source->count;
}

void region_clean(Region *region) {

  free(region->operations);

  region->operations = NULL;
  region->count = 0;
  region->capacity = 0;
}

static void region_add(WClient *client, WResource *resource, int32_t x,
                       int32_t y, int32_t width, int32_t height) {

  Region *region = wl_resource_get_user_data(resource);
  if (!region)
    return;

  region_add_operation(region, x, y, width, height, false);
}

static void region_subtract(WClient *client, WResource *resource, int32_t x,
                            int32_t y, int32_t width, int32_t height) {

  Region *region = wl_resource_get_user_data(resource);
  if (!region)
    return;

  region_add_operation(region, x, y, width, height, true);
}

static void region_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_region_interface region_implementation = {
    .destroy = region_destroy,
    .add = region_add,
    .subtract = region_subtract,
};

//the surface kept its own copy, so this frees the client's original and
//nothing else
static void destroy_region_resource(WResource *resource) {

  Region *region = wl_resource_get_user_data(resource);
  if (!region)
    return;

  region_clean(region);
  free(region);
}

void create_region(WClient *client, WResource *resource, uint32_t id) {

  Region *region = calloc(1, sizeof(Region));

  if (!region) {
    wl_client_post_no_memory(client);
    log_error("Can't allocate region");
    return;
  }

  //the region inherits the version the client bound wl_compositor at, like
  //every other child resource here
  WResource *region_resource = wl_resource_create(
      client, &wl_region_interface, wl_resource_get_version(resource), id);

  if (!region_resource) {
    free(region);
    wl_client_post_no_memory(client);
    log_error("Can't create region resource");
    return;
  }

  wl_resource_set_implementation(region_resource, &region_implementation,
                                 region, destroy_region_resource);

  log_debug("New region with ID %u", id);
}
