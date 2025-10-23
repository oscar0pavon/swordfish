#include "vulkan.h"
#include <engine/array.h>
#include <vulkan/vulkan_core.h>

#define PE_VK_PIPELINES_MAX 50

extern VkPipeline pe_vk_pipeline;

extern Array pe_vk_pipeline_infos;
extern Array pe_graphics_pipelines;


typedef struct PCreateShaderInfo{
    bool transparency;
    VkPipeline* out_pipeline;
    const char* vertex_path;
    const char* fragment_path;
    VkPipelineLayout layout;
}PCreateShaderInfo;

void pe_vk_pipelines_init();
void pe_vk_pipeline_create_layout(bool use_descriptor, VkPipelineLayout *layout,
                                  VkDescriptorSetLayout *set_layout);


void pe_vk_create_shader(PCreateShaderInfo*);
