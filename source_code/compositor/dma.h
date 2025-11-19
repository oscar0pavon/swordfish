#ifndef DMA_H
#define DMA_H

#include <stdint.h>

#include "renderer/vk_images.h"


typedef struct wl_client WClient;

#define MAX_DMA_PLANES 4

typedef struct DMAParams{
    WClient *client;
    PTexture image;
    int32_t fds[MAX_DMA_PLANES];
    uint32_t offsets[MAX_DMA_PLANES];
    uint32_t strides[MAX_DMA_PLANES];
    uint64_t modifiers[MAX_DMA_PLANES];
    uint32_t width;
    uint32_t height;
    uint32_t format;
    int num_planes;
}DMAParams;

extern uint64_t main_device_id;


void init_dma(void);

#endif
