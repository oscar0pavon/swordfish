#ifndef SWORDFISH_H
#define SWORDFISH_H

#include <engine/model.h>
#include "renderer/vulkan.h"

extern PModel main_cube;
extern PModel text_model;
extern PModel secondary_cube;

extern Camera main_camera;

extern bool finished_build;

extern bool is_drm_rendering;

void swordfish_init();

void swordfish_update_main_cube(PModel *model, uint32_t image_index);

void swordfish_draw_scene(VkCommandBuffer *cmd_buffer, uint32_t index);

#endif
