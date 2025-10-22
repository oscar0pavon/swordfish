#ifndef SWORDFISH_H
#define SWORDFISH_H

#include <engine/model.h>
#include "renderer/vulkan.h"

extern PModel main_cube;
extern PModel quad_model;

extern Camera main_camera;

extern bool finished_build;


void swordfish_init();

void swordfish_update_main_cube(PModel *model, uint32_t image_index);

#endif
