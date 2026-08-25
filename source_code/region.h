#ifndef REGION_H
#define REGION_H

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

//one wl_region.add or wl_region.subtract, kept in the order the client sent it
typedef struct RegionOperation {
  int32_t x, y, width, height;
  bool subtract;
} RegionOperation;

//a wl_region: the set of rectangles a client builds up to hand to
//wl_surface.set_input_region or set_opaque_region.
//
//it is stored as the operations rather than as a resolved set of rectangles,
//which costs nothing and answers the only question sword asks of it exactly.
//"is this point in the region" is the operations replayed in order - a point
//inside an add is in, a point inside a later subtract is out - so no region
//algebra is needed to get the right answer for an arbitrary add/subtract list
typedef struct Region {
  RegionOperation *operations;
  int count;
  int capacity;
} Region;

//wl_compositor.create_region, the request next to create_surface in the same
//interface. it cannot be left NULL: libwayland dispatches a NULL handler as a
//call and takes the compositor down with it
void create_region(WClient *client, WResource *resource, uint32_t id);

//is this point inside the region, in the surface's own coordinates
bool region_contains(Region *region, double x, double y);

//take a copy of a client's region. the protocol says the region is copied when
//it is set, so a client that goes on editing the wl_region afterwards - and gtk
//reuses them - does not change the surface it already handed it to
void region_copy(Region *destination, Region *source);

void region_clean(Region *region);

#endif
