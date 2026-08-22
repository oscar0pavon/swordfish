#ifndef CURSOR_H
#define CURSOR_H

#include <engine/model.h>
#include <engine/numbers.h>
#include <engine/renderer/render_target.h>

//the compositor's own pointer: one quad at the cursor position with the arrow
//shaded into it by shaders/cursor.frag, so there is no image to load and it
//stays sharp at whatever size it is scaled to. it carries no texture, which is
//why it is the only 2D thing here on the uniform-buffer-only descriptor layout
typedef struct Cursor {
  PModel model;
} Cursor;

extern Cursor cursor;

void cursor_init(Cursor *target);

//render_target is whichever output is being recorded - the cursor only ever
//actually draws on the one it is currently over, see swordfish_draw_scene()
void cursor_draw(Cursor *target, PRenderTarget *render_target,
                 VkCommandBuffer *cmd_buffer, u32 image_index);

void cursor_clean(Cursor *target);

#endif
