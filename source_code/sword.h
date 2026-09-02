#ifndef SWORD_H
#define SWORD_H

#include <engine/model.h>
#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan.h>

#include "outputs.h"

extern bool can_draw_surfaces;

void clean_sword();

void sword_init();

void sword_draw_scene(PRenderTarget *target, VkCommandBuffer *cmd_buffer, uint32_t index);

//pe_2d_draw_on_target() (engine2d.c), but for a quad going onto a rotated
//output: position/size are still this output's logical (already-swapped)
//coordinates, exactly what draw_surface()/cursor_draw() already compute -
//this only changes how they land on the render target's physical pixels.
//see outputs.h's SWORD_OUTPUT_ROTATE comment for the convention and
//outputs.c for why this stays out of pengine entirely
void sword_draw_rotated(PModel *model, PRenderTarget *render_target,
                        SwordOutput *out, u32 image_index, vec2 position,
                        vec2 size);

//copies every shm client's pixels onto the gpu, before pe_frame_draw() records
//anything that samples them
void begin_frame(void);

void end_frame(void);

void sword_frame_step(void);
#endif
