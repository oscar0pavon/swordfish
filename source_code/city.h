#ifndef CITY_H
#define CITY_H

#include <engine/array.h>
#include <engine/model.h>
#include <engine/numbers.h>

#include "renderer/vk_buffer.h"

//a directory drawn as a street of towers. every entry becomes one
//PInstance, and the whole skyline is a single instanced draw call
typedef struct City {
  PModel model;       //the unit box every building is a copy of
  Array buildings;    //PInstance, one per directory entry
  PBuffer instance_buffer;
  const char *path;
} City;

extern City city;

//the unit box every instanced tower is a copy of: centred on X and Y,
//spanning 0..1 on Z so scaling z grows it off the ground. shared with the
//system monitor, which draws the same box with a different instance array
void city_create_box(PModel *model);

void city_init(City *target, const char *path);

void city_draw(City *target, VkCommandBuffer *cmd_buffer, u32 image_index);

void city_clean(City *target);

#endif
