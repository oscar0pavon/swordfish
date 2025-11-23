#ifndef PE_SHADERS_H
#define PE_SHADERS_H

#include <vulkan/vulkan_core.h>
#include <stdbool.h>

typedef struct PShader{
  VkPipeline pipeline;
  VkShaderModule vertex;
  VkShaderModule fragment;
}PShader;

typedef struct PCreateShaderInfo{
    bool transparency;
    PShader *out_shader;
    const char* vertex_path;
    const char* fragment_path;
    VkPipelineLayout layout;
    VkGraphicsPipelineCreateInfo* vk_create_info;
}PCreateShaderInfo;

#endif
