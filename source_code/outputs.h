#ifndef SWORD_OUTPUTS_H
#define SWORD_OUTPUTS_H

#include <stdbool.h>
#include <stdint.h>

#include <engine/renderer/render_target.h>

//one physical monitor (DRM) or, under the nested Wayland path, the one host
//window - laid left to right in a single virtual coordinate space that
//layout.c tiles into and mouse.c's cursor moves through
typedef struct SwordOutput {
  int32_t x, y; //origin in the virtual desktop; y is always 0
  int32_t width, height; //the render target's own mode
  char name[24];
} SwordOutput;

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
