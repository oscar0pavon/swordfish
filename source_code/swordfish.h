#ifndef SWORDFISH_H
#define SWORDFISH_H

#include <engine/model.h>
#include "renderer/vulkan.h"

extern PModel main_cube;
extern PModel quad_model;

extern Camera main_camera;

extern bool finished_build;

extern VkPipeline main_cube_pipeline;
extern VkPipeline text_pipeline;

void swordfish_init();

#endif
