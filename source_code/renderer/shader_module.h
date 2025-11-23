#ifndef SHADER_MODULE_H
#define SHADER_MODULE_H

#include "vulkan.h"
#include "shaders.h"

extern VkPipelineShaderStageCreateInfo pe_vk_shaders_stages_infos[2];


void pe_vk_shader_load(PCreateShaderInfo *info);

#endif
