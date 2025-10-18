#include <engine/array.h>
#include <engine/macros.h>
#include <vulkan/vulkan.h>

#include <engine/model.h>


static VkPipelineLayout pe_vk_pipeline_layout;
static VkPipelineLayout pe_vk_pipeline_layout_with_descriptors;
static VkPipelineLayout pe_vk_pipeline_layout_skinned;

static VkDescriptorSetLayout pe_vk_descriptor_set_layout;
static VkDescriptorSetLayout pe_vk_descriptor_set_layout_with_texture;
static VkDescriptorSetLayout pe_vk_descriptor_set_layout_skinned;

void pe_vk_descriptor_pool_create(PModel *model);
void pe_vk_create_descriptor_sets(PModel *model);

void pe_vk_create_descriptor_set_layout();
void pe_vk_create_descriptor_set_layout_with_texture();
void pe_vk_create_descriptor_set_layout_skinned();
