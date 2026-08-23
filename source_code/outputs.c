#include "outputs.h"

#include <stdio.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <engine/renderer/vulkan.h>
#include "log.h"
#include "tty.h"

//INFO mesa's wsi_display takes the primary node fd radv opened for itself and
//uses it only while that fd is DRM master (wsi_display_init_wsi). swordfish
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
bool swordfish_acquire_drm_display(void) {

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

SwordfishOutput swordfish_outputs[PE_VK_MAX_RENDER_TARGETS];
int swordfish_outputs_count;

void swordfish_outputs_init(void) {

  int32_t x = 0;

  swordfish_outputs_count = pe_render_targets_count;

  for (u32 i = 0; i < pe_render_targets_count; i++) {
    PRenderTarget *target = &pe_render_targets[i];

    SwordfishOutput *out = &swordfish_outputs[i];
    out->x = x;
    out->y = 0;
    out->width = target->width;
    out->height = target->heigth;
    snprintf(out->name, sizeof(out->name), "swordfish-%u", i);

    log_info("Output %s: %ix%i at x=%i", out->name, out->width, out->height,
             out->x);

    x += out->width;
  }
}

SwordfishOutput *swordfish_output_at(double x) {

  if (swordfish_outputs_count == 0)
    return NULL;

  for (int i = 0; i < swordfish_outputs_count; i++) {
    SwordfishOutput *out = &swordfish_outputs[i];
    if (x >= out->x && x < out->x + out->width)
      return out;
  }

  //off either end of the virtual desktop - clamp to the nearest output
  //rather than answering NULL, since a cursor position always has to resolve
  //to some output
  return (x < 0) ? &swordfish_outputs[0]
                 : &swordfish_outputs[swordfish_outputs_count - 1];
}

int swordfish_output_index_at(double x) {
  SwordfishOutput *out = swordfish_output_at(x);
  return out ? (int)(out - swordfish_outputs) : 0;
}

int32_t swordfish_virtual_width(void) {

  if (swordfish_outputs_count == 0)
    return 0;

  SwordfishOutput *last = &swordfish_outputs[swordfish_outputs_count - 1];
  return last->x + last->width;
}

int32_t swordfish_max_output_height(void) {

  int32_t max = 0;

  for (int i = 0; i < swordfish_outputs_count; i++)
    if (swordfish_outputs[i].height > max)
      max = swordfish_outputs[i].height;

  return max;
}
