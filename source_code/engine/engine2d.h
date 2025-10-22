#ifndef ENGINE_2D_H
#define ENGINE_2D_H

#include <engine/model.h>

#include "renderer/cglm/cglm.h"


void pe_2d_init();
void pe_2d_create_quad_geometry(PModel* model);

void pe_2d_draw(PModel* model, u32 image_index, vec2 position, vec2 size);

extern mat4 orthogonal_projection;

#endif
