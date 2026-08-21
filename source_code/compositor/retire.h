#ifndef RETIRE_H
#define RETIRE_H

#include <engine/images.h>
#include <engine/renderer/vk_buffer.h>
#include <stdbool.h>
#include <stdint.h>

//vulkan objects a client took away while the render thread was still using
//them: the image behind a wl_buffer the client destroyed, the staging buffer
//that fed it. none of it can be destroyed where the request arrives - that is
//the compositor thread, and the render thread may be recording a draw that
//samples it at that moment - so it is parked here and destroyed later, from
//end_frame().
//
//"later" used to mean "in the next end_frame", because pe_vk_draw_frame() ended
//in a vkQueueWaitIdle() and every frame therefore left the queue empty. pengine
//runs PE_VK_FRAMES_IN_FLIGHT frames at once now and that wait is gone, so
//end_frame() is no longer a point where the gpu has finished anything. What is
//still true is that the frame at the head of the loop waits on the fence of
//the frame PE_VK_FRAMES_IN_FLIGHT back, so an object last referenced by frame
//N is finished with once PE_VK_FRAMES_IN_FLIGHT later frames have gone by - and
//that, plus one frame of slack for the frame being recorded while the
//retirement lands, is when this list frees it
void retire_texture(const PTexture *texture);

void retire_buffer(const PBuffer *buffer);

//destroy everything no frame can still be reading, and count the frame. called
//once per frame from end_frame(), on the render thread
void retire_collect(void);

//the frame being drawn now. what a caller records against something it wants
//dropped later - see task_release_old_buffer()
uint64_t retire_frame_number(void);

//has every frame that could have referenced something recorded at that frame
//number finished on the gpu
bool retire_frame_is_finished(uint64_t frame);

#endif
