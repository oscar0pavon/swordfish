#include "outputs.h"

#include <stdio.h>

#include <engine/renderer/vulkan.h>

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

    printf("Output %s: %ix%i at x=%i\n", out->name, out->width, out->height,
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
