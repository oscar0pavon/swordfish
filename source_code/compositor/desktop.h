#ifndef DESKTOP_H
#define DESKTOP_H

#include "compositor.h"
#include "desktop-server.h"
#include "surface.h"
#include <stdint.h>

typedef struct DesktopSurface{
  SwordfishSurface* surface;
  WaylandResource* resource;
  uint32_t pending_serial;
}DesktopSurface;


void bind_desktop(WaylandClient *client, void *data, uint32_t version,
                       uint32_t id);
#endif
