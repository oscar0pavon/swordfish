#ifndef DMA_H
#define DMA_H

#include <stdint.h>

#include "linux-dmabuf.h"

extern uint64_t main_device_id;

extern struct zwp_linux_dmabuf_v1_interface dmabuf_data;

void init_dma(void);

#endif
