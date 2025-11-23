#include "shader_module.h"
#include "renderer/shaders.h"
#include "vulkan.h"
#include <engine/file_loader.h>
#include <engine/log.h>
#include "pipeline.h"

VkPipelineShaderStageCreateInfo pe_vk_shaders_stages_infos[2];

VkShaderModule pe_vk_shader_module_create(File *file) {
  VkShaderModuleCreateInfo info;
  ZERO(info);
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = file->size_in_bytes;
  info.pCode = file->data;

  VkShaderModule module;
  VKVALID(vkCreateShaderModule(vk_device, &info, NULL, &module),
          "Can't create Shader Module");
  return module;
}

//load .spv built shaders
void pe_vk_shader_load(PCreateShaderInfo *info) {
  File new_file;
  ZERO(new_file);
  pe_file_openb(info->vertex_path, &new_file);

  File new_file2;
  ZERO(new_file2);
  pe_file_openb(info->fragment_path, &new_file2);


  VkShaderModule vertex_module = pe_vk_shader_module_create(&new_file);
  VkShaderModule fragment_module = pe_vk_shader_module_create(&new_file2);

  info->out_shader->vertex = vertex_module;
  info->out_shader->fragment= fragment_module;
  

  VkPipelineShaderStageCreateInfo info1;
  ZERO(info1);
  info1.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info1.stage = VK_SHADER_STAGE_VERTEX_BIT;
  info1.module = vertex_module;
  info1.pName = "main";

  VkPipelineShaderStageCreateInfo info2;
  ZERO(info2);
  info2.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info2.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  info2.module = fragment_module;
  info2.pName = "main";

  shader_create_info[0] = info1;
  shader_create_info[1] = info2;

  info->vk_create_info->pStages = shader_create_info;

}
