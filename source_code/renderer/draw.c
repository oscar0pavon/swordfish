#include "draw.h"
#include "commands.h"
#include "descriptor_set.h"
#include "pipeline.h"
#include "swap_chain.h"
#include "swordfish.h"
#include "sync.h"
#include "uniform_buffer.h"
#include "vk_vertex.h"
#include "vulkan.h"
#include <engine/macros.h>
#include <vulkan/vulkan_core.h>


void pe_vk_draw_commands(VkCommandBuffer *cmd_buffer, uint32_t index) {

  vkCmdSetViewport(*(cmd_buffer), 0, 1, &viewport);

  vkCmdSetScissor(*(cmd_buffer), 0, 1, &scissor);

  VkDeviceSize offsets[] = {0};

  VkDescriptorSet *descriptor_set = NULL;

  // TODO draw objets here

  VkPipeline *uniform = array_get(&pe_graphics_pipelines, 0);
  pe_vk_uniform_buffer_update_two(&main_cube, index);
  descriptor_set = array_get(&main_cube.descriptor_sets, index);

  vkCmdBindDescriptorSets(*(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pe_vk_pipeline_layout_with_descriptors, 0, 1,
                          descriptor_set, 0, NULL);
  vkCmdBindPipeline(*(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, *(uniform));

  vkCmdBindVertexBuffers(*(cmd_buffer), 0, 1, &main_cube.vertex_buffer,
                         offsets);
  vkCmdBindIndexBuffer(*(cmd_buffer), main_cube.index_buffer, 0,
                       VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(*(cmd_buffer), main_cube.index_array.count, 1, 0, 0, 0);
}

void pe_vk_draw_frame() {

  vkWaitForFences(vk_device, 1, &pe_vk_fence_in_flight, VK_TRUE, UINT64_MAX);
  vkResetFences(vk_device, 1, &pe_vk_fence_in_flight);

  uint32_t image_index;

  vkAcquireNextImageKHR(vk_device, pe_vk_swap_chain, UINT64_MAX,
                        pe_vk_semaphore_images_available, VK_NULL_HANDLE,
                        &image_index);

  // pe_vk_uniform_buffer_update(image_index);
  pe_vk_record_commands_buffer(image_index);//INFO this is where we draw things

  VkSemaphore singal_semaphore[] = {pe_vk_semaphore_render_finished};
  VkSemaphore wait_semaphores[] = {pe_vk_semaphore_images_available};
  
  VkCommandBuffer *command_buffer = array_get(&pe_vk_command_buffers, image_index);

  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  VkSwapchainKHR swap_chains[] = {pe_vk_swap_chain};

  VkSubmitInfo submit_info = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .waitSemaphoreCount = 1,
                              .pWaitSemaphores = wait_semaphores,
                              .pWaitDstStageMask = wait_stages,
                              .commandBufferCount = 1,
                              .pCommandBuffers = command_buffer,
                              .signalSemaphoreCount = 1,
                              .pSignalSemaphores = singal_semaphore};

  vkQueueSubmit(vk_queue, 1, &submit_info, pe_vk_fence_in_flight);

  VkPresentInfoKHR present_info = {

      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = singal_semaphore,
      .swapchainCount = 1,
      .pSwapchains = swap_chains,
      .pImageIndices = &image_index};

  VKVALID(vkQueuePresentKHR(vk_queue, &present_info), "Can't present");

  vkQueueWaitIdle(vk_queue);
}
