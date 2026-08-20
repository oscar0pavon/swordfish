#ifndef SWORDFISH_H
#define SWORDFISH_H

#include <engine/model.h>
#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan.h>

extern PModel text_model;

extern bool finished_build;

extern bool can_draw_surfaces;

void clean_swordfish();

void swordfish_init();

void swordfish_draw_scene(VkCommandBuffer *cmd_buffer, uint32_t index);

void end_frame(void);
#endif
