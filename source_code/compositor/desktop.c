#include "desktop.h"
#include "compositor.h"
#include <complex.h>

//recieve from client
void do_desktop_ack(WaylandClient* client, WaylandResource* resource, uint32_t serial){

}

void do_desktop_pong(WaylandClient* client, WaylandResource* resource, uint32_t serial){
  //TODO respond to client
}

void get_desktop_surface(WaylandClient *client, WaylandResource *resource,
    uint32_t id, WaylandResource *surface_resource) {

  SwordfishCompositor* compositor = wl_resource_get_user_data(resource);
  SwordfishSurface* surface = wl_resource_get_user_data(surface_resource);




}

struct xdg_wm_base_interface desktop_implementation = {
  .destroy = NULL,
  .get_xdg_surface = get_desktop_surface,
  .pong = do_desktop_pong
};




void get_shell_surface(WaylandClient *client, WaylandResource *resource,
                       uint32_t id, WaylandResource *surface_resource) {


}

const struct xdg_surface_interface xdg_surface_implementation = {
    .destroy = NULL,
    .get_toplevel = NULL,//TODO get top level
    .get_popup = NULL,
    .set_window_geometry = NULL,
    .ack_configure = do_desktop_ack,
};


const struct wl_shell_interface shell_interface = {
    .get_shell_surface = get_shell_surface,
};

