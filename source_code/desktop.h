#ifndef DESKTOP_H
#define DESKTOP_H

#include "compositor.h"
#include "desktop-server.h"
#include "surface.h"
#include <stdint.h>

typedef struct DesktopSurface{
  Task* surface;
  WResource* resource;
  //the serial of the configure the client still owes an ack for, 0 when it
  //owes nothing
  uint32_t pending_serial;
  //xdg_surface.set_window_geometry: the window without the client's own
  //shadows. this, not the buffer, is what the layout's cell sizes - see
  //task_window_geometry() (subcompositor.c)
  int32_t geometry_x;
  int32_t geometry_y;
  int32_t geometry_width;
  int32_t geometry_height;
}DesktopSurface;


void bind_desktop(WClient *client, void *data, uint32_t version,
                       uint32_t id);
#endif
