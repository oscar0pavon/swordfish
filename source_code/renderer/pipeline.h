#include "renderer/shaders.h"
#include "vulkan.h"
#include <engine/array.h>
#include <vulkan/vulkan_core.h>

#include "shaders.h"

#define PE_VK_PIPELINES_MAX 50

extern VkPipeline pe_vk_pipeline;

extern Array pe_vk_pipeline_infos;
extern Array pe_graphics_pipelines;

extern VkPipelineShaderStageCreateInfo shader_create_info[2];


void pe_vk_pipelines_init();
void pe_vk_pipeline_create_layout(bool use_descriptor, VkPipelineLayout *layout,
                                  VkDescriptorSetLayout *set_layout);


void pe_vk_create_shader(PCreateShaderInfo*);
