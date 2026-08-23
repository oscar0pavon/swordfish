#include "retire.h"

#include <engine/renderer/sync.h>
#include <engine/renderer/vk_images.h>
#include <engine/renderer/vulkan.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>

typedef enum RetiredKind {
  RETIRED_TEXTURE,
  RETIRED_BUFFER
} RetiredKind;

typedef struct Retired {
  RetiredKind kind;
  PTexture texture;
  PBuffer buffer;
  //the frame it was handed over in, which is the last frame that can possibly
  //have recorded a command referring to it
  uint64_t frame;
} Retired;

#define MAX_RETIRED 128

static Retired retired[MAX_RETIRED];
static int retired_count;

//counted by retire_collect(), so it is the number of the frame end_frame() is
//finishing while anything below reads it
static uint64_t frame_number;

static Retired *retire_slot(void) {

  if (retired_count >= MAX_RETIRED) {
    printf("Too many retired vulkan objects, leaking one\n");
    return NULL;
  }

  Retired *slot = &retired[retired_count++];

  *slot = (Retired){0};
  slot->frame = frame_number;

  return slot;
}

void retire_texture(const PTexture *texture) {

  //a surface destroyed before its first upload has no image, and
  //pe_vk_clean_image() reads straight through the pointer
  if (texture->image == VK_NULL_HANDLE)
    return;

  Retired *slot = retire_slot();
  if (slot) {
    slot->kind = RETIRED_TEXTURE;
    slot->texture = *texture;
  }
}

void retire_buffer(const PBuffer *buffer) {

  if (buffer->buffer == VK_NULL_HANDLE)
    return;

  Retired *slot = retire_slot();
  if (slot) {
    slot->kind = RETIRED_BUFFER;
    slot->buffer = *buffer;
  }
}

uint64_t retire_frame_number(void) {
  return frame_number;
}

bool retire_frame_is_finished(uint64_t frame) {
  return frame_number - frame > PE_VK_FRAMES_IN_FLIGHT;
}

void retire_collect(void) {

  int kept = 0;

  for (int i = 0; i < retired_count; i++) {

    //still inside the window the gpu may be reading it in. the margin is one
    //more than the frames in flight because a resource can be retired by a
    //request handler mid-dispatch, before this iteration's own frame step has
    //advanced frame_number - so the recorded frame number can be one short of
    //the last frame that could reference the object. the list is compacted
    //rather than walked in order, since a later entry can come due before an
    //earlier one only if the counter wrapped
    if (frame_number - retired[i].frame <= PE_VK_FRAMES_IN_FLIGHT) {
      retired[kept++] = retired[i];
      continue;
    }

    switch (retired[i].kind) {
    case RETIRED_TEXTURE:
      pe_vk_clean_image(&retired[i].texture);
      break;
    case RETIRED_BUFFER:
      vkDestroyBuffer(vk_device, retired[i].buffer.buffer, NULL);
      vkFreeMemory(vk_device, retired[i].buffer.memory, NULL);
      break;
    }
  }

  retired_count = kept;

  frame_number++;
}
