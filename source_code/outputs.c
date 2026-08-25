#include "outputs.h"

#include <errno.h>
#include <stdio.h>
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
    out->width = target->width;
    out->height = target->heigth;
    snprintf(out->name, sizeof(out->name), "sword-%u", i);

    log_info("Output %s: %ix%i at x=%i", out->name, out->width, out->height,
             out->x);

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
