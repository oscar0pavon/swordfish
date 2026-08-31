#include "outputs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <engine/renderer/display.h>
#include <engine/renderer/vulkan.h>
#include "log.h"
#include "tty.h"

//INFO mesa's wsi_display takes the primary node fd radv opened for itself and
//uses it only while that fd is DRM master (wsi_display_init_wsi). sword
//takes master first, in tty_session_init(), precisely so that check fails and
//wsi is left with no fd - which is what makes the acquire below succeed and
//install *ours*. after that every enumeration, mode set and page flip goes
//through an fd this process owns, so drmDropMaster() on a VT switch actually
//lets go of the screen.
//
//one acquire is all mesa supports (its own comment says no multiple leases),
//and one is all that is needed: installing the fd serves every connector on
//the device, so the ordinary vkGetPhysicalDeviceDisplayPropertiesKHR walk in
//pengine's vk_get_displays() finds them all afterwards
bool sword_acquire_drm_display(void) {

  int drm_fd = tty_drm_fd();
  if (drm_fd < 0) {
    log_warn("No DRM fd of our own, letting mesa open its own display");
    return false;
  }

  PFN_vkGetDrmDisplayEXT get_drm_display =
      (PFN_vkGetDrmDisplayEXT)vkGetInstanceProcAddr(vk_instance,
                                                    "vkGetDrmDisplayEXT");
  PFN_vkAcquireDrmDisplayEXT acquire_drm_display =
      (PFN_vkAcquireDrmDisplayEXT)vkGetInstanceProcAddr(
          vk_instance, "vkAcquireDrmDisplayEXT");

  if (!get_drm_display || !acquire_drm_display) {
    log_error("VK_EXT_acquire_drm_display is not available");
    return false;
  }

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_error("drmModeGetResources failed on our DRM fd");
    return false;
  }

  bool acquired = false;

  for (int i = 0; i < resources->count_connectors && !acquired; i++) {

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, resources->connectors[i]);
    if (!connector)
      continue;

    //an unplugged connector has no display behind it to acquire
    if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes) {

      VkDisplayKHR display;
      VkResult result = get_drm_display(vk_physical_device, drm_fd,
                                        connector->connector_id, &display);

      if (result == VK_SUCCESS) {
        result = acquire_drm_display(vk_physical_device, drm_fd, display);

        if (result == VK_SUCCESS) {
          log_info("Acquired DRM display on connector %u through our own fd",
                   connector->connector_id);
          acquired = true;
        } else {
          log_error("vkAcquireDrmDisplayEXT failed on connector %u (%i)",
                    connector->connector_id, result);
        }
      } else {
        log_warn("vkGetDrmDisplayEXT failed on connector %u (%i)",
                 connector->connector_id, result);
      }
    }

    drmModeFreeConnector(connector);
  }

  drmModeFreeResources(resources);

  if (!acquired)
    log_error("No DRM connector could be acquired");

  return acquired;
}

//INFO vk_get_displays() (pengine's display.c) orders pe_vk_displays[] however
//vkGetPhysicalDeviceDisplayPropertiesKHR happened to walk them, which is
//RADV's own internal connector probe order and not the raw DRM connector list
//- the same raw list sword itself walks in sword_acquire_drm_display() above,
//and the one a host compositor driving the same box (sway on another VT, or
//before this one started) also walks, since nothing outside Vulkan enumerates
//outputs any other way. Left unsorted, sword's left-to-right layout
//(sword_outputs_init(), out->x = 0, out->width, out->width + next->width...)
//can come out the opposite of what the same two monitors show under sway,
//which reads as "the outputs swapped" on every switch to sword's VT even
//though nothing changes at runtime - the order is fixed once here, at start,
//and never revisited (see tty.c's session_activate(): a VT switch retakes DRM
//master, nothing more).
void sword_sort_displays_by_connector(void) {

  int drm_fd = tty_drm_fd();
  if (drm_fd < 0)
    return;

  PFN_vkGetDrmDisplayEXT get_drm_display =
      (PFN_vkGetDrmDisplayEXT)vkGetInstanceProcAddr(vk_instance,
                                                    "vkGetDrmDisplayEXT");
  if (!get_drm_display) {
    log_warn("VK_EXT_acquire_drm_display is not available, leaving "
             "display order as enumerated");
    return;
  }

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_warn("Can't enumerate connectors to order the outputs: %s",
             strerror(errno));
    return;
  }

  PVkDisplay sorted[PE_VK_MAX_RENDER_TARGETS];
  u32 sorted_count = 0;

  for (int i = 0; i < resources->count_connectors &&
                  sorted_count < pe_vk_displays_count;
       i++) {

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, resources->connectors[i]);
    if (!connector)
      continue;

    if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes) {

      VkDisplayKHR display;
      if (get_drm_display(vk_physical_device, drm_fd, connector->connector_id,
                          &display) == VK_SUCCESS) {

        //match it back to the Vulkan display vk_get_displays() already picked
        //a mode and a plane for, rather than re-deriving either here
        for (u32 d = 0; d < pe_vk_displays_count; d++) {
          if (pe_vk_displays[d].display == display) {
            sorted[sorted_count++] = pe_vk_displays[d];
            break;
          }
        }
      }
    }

    drmModeFreeConnector(connector);
  }

  drmModeFreeResources(resources);

  //every display vk_get_displays() enumerated has to have matched a
  //connector, or the reorder is short one and pe_render_targets would come up
  //missing whatever got dropped
  if (sorted_count != pe_vk_displays_count) {
    log_warn("Matched %u of %u displays to connectors, leaving display "
             "order as enumerated", sorted_count, pe_vk_displays_count);
    return;
  }

  memcpy(pe_vk_displays, sorted, sizeof(PVkDisplay) * sorted_count);

  log_info("Reordered %u displays to match the DRM connector list", sorted_count);
}

//INFO diagnostic only. mesa's wsi_display (src/vulkan/wsi/wsi_common_display.c)
//caches connector->crtc_id the first time it successfully modesets a
//connector, and only re-derives it via wsi_display_select_crtc() when
//connector->active was false - which losing DRM master forces. Each
//connector's atomic commit on the way back is independent, so two connectors
//can each land on a CRTC that swapped which physical monitor it drives while
//we did not hold master - sword's own task->output_index and
//pe_render_targets[] never change, only the CRTC underneath one of them does.
//This logs the kernel's own connector->crtc routing so that can be confirmed
//against /tmp/sword.log rather than inferred
void sword_log_display_routing(const char *when) {

  int drm_fd = tty_drm_fd();
  if (drm_fd < 0)
    return;

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_warn("Can't enumerate connectors to log routing (%s): %s", when,
             strerror(errno));
    return;
  }

  for (int i = 0; i < resources->count_connectors; i++) {

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, resources->connectors[i]);
    if (!connector)
      continue;

    if (connector->connection == DRM_MODE_CONNECTED) {

      uint32_t crtc_id = 0;
      if (connector->encoder_id) {
        drmModeEncoder *encoder =
            drmModeGetEncoder(drm_fd, connector->encoder_id);
        if (encoder) {
          crtc_id = encoder->crtc_id;
          drmModeFreeEncoder(encoder);
        }
      }

      log_info("Display routing (%s): connector %u -> crtc %u", when,
              connector->connector_id, crtc_id);
    }

    drmModeFreeConnector(connector);
  }

  drmModeFreeResources(resources);
}

//INFO the connector->crtc pairing sword_capture_display_routing() recorded at
//startup - each render target's plane is wired to one crtc for the life of
//the process (see the routing comment above), so this is what
//sword_restore_display_routing() puts back after a VT round-trip moves it
typedef struct DisplayRoute {
  uint32_t connector_id;
  uint32_t crtc_id;
} DisplayRoute;

static DisplayRoute display_routes[PE_VK_MAX_RENDER_TARGETS];
static int display_routes_count;

//called once at startup, right after sword_sort_displays_by_connector() -
//records which crtc the kernel had driving each connected connector, which is
//also the crtc every render target's plane is permanently paired with (a
//plane's possible_crtcs is a hardware fact, and mesa picks the plane to match
//whatever crtc the connector was on when it first modeset it)
void sword_capture_display_routing(void) {

  int drm_fd = tty_drm_fd();
  if (drm_fd < 0)
    return;

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_warn("Can't enumerate connectors to capture routing: %s",
             strerror(errno));
    return;
  }

  display_routes_count = 0;

  for (int i = 0; i < resources->count_connectors &&
                  display_routes_count < PE_VK_MAX_RENDER_TARGETS;
       i++) {

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, resources->connectors[i]);
    if (!connector)
      continue;

    if (connector->connection == DRM_MODE_CONNECTED && connector->encoder_id) {

      drmModeEncoder *encoder =
          drmModeGetEncoder(drm_fd, connector->encoder_id);
      if (encoder && encoder->crtc_id) {
        DisplayRoute *route = &display_routes[display_routes_count++];
        route->connector_id = connector->connector_id;
        route->crtc_id = encoder->crtc_id;
      }
      if (encoder)
        drmModeFreeEncoder(encoder);
    }

    drmModeFreeConnector(connector);
  }

  drmModeFreeResources(resources);
}

//called from tty.c's session_activate(), right after drmSetMaster() and
//before sword's next present: another DRM master (sway, on another VT) may
//have reassigned which connector each crtc drives while we did not hold
//master. A render target's plane cannot follow - it is wired to one crtc for
//good - so this puts each connector back on the crtc it was captured on,
//which is what keeps a render target's fixed plane landing on the monitor it
//was chosen for. Snapshots each moved connector's own currently-valid
//mode/framebuffer before changing anything, and applies all the moves from
//those snapshots - not by reading the target crtc's state, which for a
//two-way swap is the very thing about to be vacated
void sword_restore_display_routing(void) {

  int drm_fd = tty_drm_fd();
  if (drm_fd < 0 || display_routes_count == 0)
    return;

  typedef struct {
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t buffer_id;
    drmModeModeInfo mode;
  } PendingMove;

  PendingMove pending[PE_VK_MAX_RENDER_TARGETS];
  int pending_count = 0;

  for (int i = 0; i < display_routes_count; i++) {

    DisplayRoute *route = &display_routes[i];

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, route->connector_id);
    if (!connector)
      continue;

    uint32_t current_crtc_id = 0;
    if (connector->encoder_id) {
      drmModeEncoder *encoder =
          drmModeGetEncoder(drm_fd, connector->encoder_id);
      if (encoder) {
        current_crtc_id = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
      }
    }
    drmModeFreeConnector(connector);

    //already on the crtc its plane wants - nothing to do
    if (current_crtc_id == route->crtc_id)
      continue;

    if (current_crtc_id == 0) {
      log_warn("Connector %u has no crtc at all, can't move it back onto "
               "crtc %u", route->connector_id, route->crtc_id);
      continue;
    }

    //the mode and framebuffer already valid on whichever crtc this connector
    //is currently (wrongly) driven by - taken before either of the two
    //connectors involved in a swap gets moved, so neither snapshot depends on
    //the other's about-to-change state
    drmModeCrtc *crtc = drmModeGetCrtc(drm_fd, current_crtc_id);
    if (!crtc || !crtc->mode_valid || crtc->buffer_id == 0) {
      log_warn("Connector %u's current crtc %u has no valid mode/framebuffer "
               "to move, can't restore it onto crtc %u", route->connector_id,
               current_crtc_id, route->crtc_id);
      if (crtc)
        drmModeFreeCrtc(crtc);
      continue;
    }

    PendingMove *move = &pending[pending_count++];
    move->connector_id = route->connector_id;
    move->crtc_id = route->crtc_id;
    move->buffer_id = crtc->buffer_id;
    move->mode = crtc->mode;

    drmModeFreeCrtc(crtc);
  }

  for (int i = 0; i < pending_count; i++) {
    PendingMove *move = &pending[i];

    if (drmModeSetCrtc(drm_fd, move->crtc_id, move->buffer_id, 0, 0,
                       &move->connector_id, 1, &move->mode) < 0)
      log_warn("Can't restore connector %u onto crtc %u: %s",
               move->connector_id, move->crtc_id, strerror(errno));
    else
      log_info("Restored connector %u onto crtc %u", move->connector_id,
               move->crtc_id);
  }
}

//true if SWORD_OUTPUT_ROTATE lists this output index - see outputs.h
static bool output_rotate_requested(int index) {

  const char *list = getenv("SWORD_OUTPUT_ROTATE");
  if (!list)
    return false;

  //no strtol here: an index is one digit for every output count this table
  //will ever hold (PE_VK_MAX_RENDER_TARGETS is 4), so a byte-by-byte compare
  //against each comma-delimited entry is simpler than pulling in parsing
  char target[4];
  snprintf(target, sizeof(target), "%d", index);
  size_t target_len = strlen(target);

  const char *p = list;
  while (*p) {
    while (*p == ' ')
      p++;
    const char *comma = strchr(p, ',');
    size_t entry_len = comma ? (size_t)(comma - p) : strlen(p);

    if (entry_len == target_len && strncmp(p, target, entry_len) == 0)
      return true;

    if (!comma)
      break;
    p = comma + 1;
  }

  return false;
}

SwordOutput sword_outputs[PE_VK_MAX_RENDER_TARGETS];
int sword_outputs_count;

void sword_outputs_init(void) {

  int32_t x = 0;

  sword_outputs_count = pe_render_targets_count;

  for (u32 i = 0; i < pe_render_targets_count; i++) {
    PRenderTarget *target = &pe_render_targets[i];

    SwordOutput *out = &sword_outputs[i];
    out->x = x;
    out->y = 0;
    out->rotated = output_rotate_requested((int)i);

    //a 90 degree rotation swaps which physical axis is which logical one -
    //everything past this point (layout, the cursor, mouse.c's clamp) reads
    //out->width/height and never target->width/heigth directly, so nothing
    //downstream has to know rotation happened at all
    out->width = out->rotated ? (int32_t)target->heigth : (int32_t)target->width;
    out->height = out->rotated ? (int32_t)target->width : (int32_t)target->heigth;

    snprintf(out->name, sizeof(out->name), "sword-%u", i);

    log_info("Output %s: %ix%i at x=%i%s", out->name, out->width, out->height,
             out->x, out->rotated ? " (rotated 90)" : "");

    x += out->width;
  }
}

SwordOutput *sword_output_at(double x) {

  if (sword_outputs_count == 0)
    return NULL;

  for (int i = 0; i < sword_outputs_count; i++) {
    SwordOutput *out = &sword_outputs[i];
    if (x >= out->x && x < out->x + out->width)
      return out;
  }

  //off either end of the virtual desktop - clamp to the nearest output
  //rather than answering NULL, since a cursor position always has to resolve
  //to some output
  return (x < 0) ? &sword_outputs[0]
                 : &sword_outputs[sword_outputs_count - 1];
}

int sword_output_index_at(double x) {
  SwordOutput *out = sword_output_at(x);
  return out ? (int)(out - sword_outputs) : 0;
}

int32_t sword_virtual_width(void) {

  if (sword_outputs_count == 0)
    return 0;

  SwordOutput *last = &sword_outputs[sword_outputs_count - 1];
  return last->x + last->width;
}

int32_t sword_max_output_height(void) {

  int32_t max = 0;

  for (int i = 0; i < sword_outputs_count; i++)
    if (sword_outputs[i].height > max)
      max = sword_outputs[i].height;

  return max;
}
