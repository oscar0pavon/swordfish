#ifndef SWORD_OUTPUTS_H
#define SWORD_OUTPUTS_H

#include <stdbool.h>
#include <stdint.h>

#include <engine/renderer/render_target.h>

//one physical monitor, laid left to right in a single virtual coordinate
//space that layout.c tiles into and mouse.c's cursor moves through.
//
//the table's own order is the render target order and never changes (see
//sword_outputs_init()), but the x origins are handed out rotated outputs
//first, so a portrait monitor sits at the left of the virtual desktop. index
//order is therefore not left-to-right order, and nothing may take
//sword_outputs[0] for the leftmost output or the last entry for the rightmost
typedef struct SwordOutput {
  int32_t x, y; //origin in the virtual desktop; y is always 0
  int32_t width, height; //logical size - width/height are swapped from the
                         //render target's own mode when rotated is true
  char name[24];

  //true if this output is mounted rotated 90 degrees counter-clockwise (the
  //panel's native right edge points visually up). set once in
  //sword_outputs_init() from SWORD_OUTPUT_ROTATE and never revisited.
  //everything downstream - layout.c, mouse.c, sword.c's draw_surface(),
  //cursor.c - works entirely in this output's logical (already-rotated)
  //width/height and never needs to read this flag at all. sword_draw_rotated()
  //(sword.h) is the one place that looks past it to the physical render
  //target underneath, at draw time
  bool rotated;
} SwordOutput;

//SWORD_OUTPUT_ROTATE is a comma-separated list of output indices (0-based,
//in the connector order sword_sort_displays_by_connector() already fixed) to
//mount rotated 90 degrees counter-clockwise, e.g. "1" or "0,2". mesa's
//wsi_display hardcodes VkDisplayPropertiesKHR.supportedTransforms to
//IDENTITY only (src/vulkan/wsi/wsi_common_display.c), so there is no
//hardware rotation to ask the display plane for here - this is entirely a
//software rotation of what sword draws, done once per quad in
//sword_draw_rotated()

extern SwordOutput sword_outputs[PE_VK_MAX_RENDER_TARGETS];
extern int sword_outputs_count;

//pengine's pe_vk_acquire_display hook, wired up in main() on the DRM path.
//hands vulkan the DRM fd tty.c holds master on, so mesa scans out through an
//fd sword can drop again when the VT is switched away
bool sword_acquire_drm_display(void);

//pengine's pe_vk_sort_displays hook, wired up in main() on the DRM path.
//reorders pe_vk_displays[] to match the raw DRM connector list order rather
//than whatever order the Vulkan driver's own connector probe walked them in -
//see outputs.c for why those two can disagree
void sword_sort_displays_by_connector(void);

//diagnostic only: logs which CRTC the kernel has driving each connected
//connector right now. called once at startup (main()) and again from
//tty.c's session_deactivate()/session_activate() around a VT switch, so the
//routing before and after can be compared in /tmp/sword.log
void sword_log_display_routing(const char *when);

//records the connector->crtc pairing every render target's plane is
//permanently wired to. called once at startup (main()), right after
//sword_sort_displays_by_connector()
void sword_capture_display_routing(void);

//puts the pairing sword_capture_display_routing() recorded back, in case
//another DRM master moved it while sword did not hold master. called from
//tty.c's session_activate(), right after drmSetMaster() and before sword's
//next present
void sword_restore_display_routing(void);

//fills the table from pe_render_targets, one output per target, in order.
//called once from main() right after pe_vk_init() - before the compositor
//thread starts, so nothing needs a lock to read it afterward
void sword_outputs_init(void);

//the output containing virtual x, or the nearest one if x falls outside the
//virtual desktop entirely. NULL only if no output was ever set up
SwordOutput *sword_output_at(double x);
int sword_output_index_at(double x);

//sum of every output's width - what the cursor clamps to and what an
//absolute pointer device (a tablet, a touchscreen) is scaled against
int32_t sword_virtual_width(void);

//the tallest output. an absolute pointer device reports one axis in a space
//that has to be scaled against some single height, and outputs of different
//physical size have no one right answer - the tallest wastes the least of it
int32_t sword_max_output_height(void);

#endif
