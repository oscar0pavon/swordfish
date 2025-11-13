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

extern struct xdg_wm_base_interface desktop_implementation;

#endif
