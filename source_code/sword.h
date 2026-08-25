#ifndef SWORD_H
#define SWORD_H

#include <engine/model.h>
#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan.h>

extern bool can_draw_surfaces;

void clean_sword();

void sword_init();

void sword_draw_scene(PRenderTarget *target, VkCommandBuffer *cmd_buffer, uint32_t index);

void end_frame(void);

void sword_frame_step(void);
#endif
