#ifndef SWORD_H
#define SWORD_H

#include <engine/model.h>
#include <engine/renderer/renderer.h>
#include <engine/renderer/vulkan.h>


extern bool can_draw_surfaces;

void clean_sword();

void sword_init();

//defined in main.c, called from sword_init() to catch ctrl+c / SIGTERM
void handle_signal(int sig_num);

#endif
