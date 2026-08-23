#ifndef WTYPES_H
#define WTYPES_H

#include <wayland-server-core.h>
#include <wayland-server.h>
#include "desktop-server.h"

typedef struct wl_resource WResource;
typedef struct wl_client WClient;
typedef struct wl_compositor_interface WaylanCompositorInterface;
typedef struct xdg_wm_base DesktopBase;

#endif
