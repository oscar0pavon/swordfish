#ifndef SWORDFISH_OUTPUTS_H
#define SWORDFISH_OUTPUTS_H

#include <stdbool.h>
#include <stdint.h>

#include <engine/renderer/render_target.h>

//one physical monitor (DRM) or, under the nested Wayland path, the one host
//window - laid left to right in a single virtual coordinate space that
//layout.c tiles into and mouse.c's cursor moves through
typedef struct SwordfishOutput {
  int32_t x, y; //origin in the virtual desktop; y is always 0
  int32_t width, height; //the render target's own mode
  char name[24];
} SwordfishOutput;

extern SwordfishOutput swordfish_outputs[PE_VK_MAX_RENDER_TARGETS];
extern int swordfish_outputs_count;

//pengine's pe_vk_acquire_display hook, wired up in main() on the DRM path.
//hands vulkan the DRM fd tty.c holds master on, so mesa scans out through an
//fd swordfish can drop again when the VT is switched away
bool swordfish_acquire_drm_display(void);

//fills the table from pe_render_targets, one output per target, in order.
//called once from main() right after pe_vk_init() - before the compositor
//thread starts, so nothing needs a lock to read it afterward
void swordfish_outputs_init(void);

//the output containing virtual x, or the nearest one if x falls outside the
//virtual desktop entirely. NULL only if no output was ever set up
SwordfishOutput *swordfish_output_at(double x);
int swordfish_output_index_at(double x);

//sum of every output's width - what the cursor clamps to and what an
//absolute pointer device (a tablet, a touchscreen) is scaled against
int32_t swordfish_virtual_width(void);

//the tallest output. an absolute pointer device reports one axis in a space
//that has to be scaled against some single height, and outputs of different
//physical size have no one right answer - the tallest wastes the least of it
int32_t swordfish_max_output_height(void);

#endif
