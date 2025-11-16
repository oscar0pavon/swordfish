#define _GNU_SOURCE
#include "feedback.h"
#include "dma.h"
#include "linux-dmabuf.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <unistd.h>
#include "direct_render.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <drm/drm_fourcc.h>
#include <string.h>

typedef struct FormatTable {
  uint32_t format;
  uint32_t padding;
  uint32_t modifer;
}FormatTable;

static int create_anon_file(size_t size) {
    int fd = memfd_create("dmabuf-feedback", MFD_CLOEXEC);
    if (fd >= 0) {
        ftruncate(fd, size);
        return fd;
    }
    // Fallback for systems without memfd_create
    // ... (omitted for brevity, but a robust compositor needs this) ...
    return -1;
}

void send_format_table(WaylandResource* resource) {
    printf("Compositor sending format table via shared memory\n");

    FormatTable supported_formats[] = {
        { DRM_FORMAT_XRGB8888, 0, DRM_FORMAT_MOD_LINEAR },
    };

    size_t num_formats = sizeof(supported_formats) / sizeof(supported_formats[0]);
    size_t table_size = num_formats * sizeof(FormatTable);

    int fd = create_anon_file(table_size);
    if (fd < 0) {
        fprintf(stderr, "Failed to create shared memory file\n");
        return;
    }

  void *map = mmap(NULL, table_size, PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        fprintf(stderr, "Failed to mmap shared memory\n");
        return;
    }
    memcpy(map, supported_formats, table_size);
    munmap(map, table_size);

    zwp_linux_dmabuf_feedback_v1_send_format_table(resource, fd, table_size);


}



