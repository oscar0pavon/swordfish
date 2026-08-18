#ifndef HUD_H
#define HUD_H

#include <engine/array.h>
#include <engine/model.h>
#include <engine/numbers.h>

//the flat overlay carrying the numbers the 3D scene can only suggest. every
//character is a quad, and the quad count is fixed so the index count never
//changes: unused characters collapse to zero area instead
#define HUD_MAX_CHARS 256

typedef struct Hud {
  PModel model;
  char text[HUD_MAX_CHARS + 1];
  float next_update_seconds;
} Hud;

extern Hud hud;

void hud_init(Hud *target);

void hud_draw(Hud *target, VkCommandBuffer *cmd_buffer, u32 image_index);

void hud_clean(Hud *target);

#endif
