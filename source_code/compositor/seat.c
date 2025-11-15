#include "seat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libseat.h> // Make sure you have this include
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h> 
#include <poll.h> 
#include "compositor.h"
#include <pthread.h>

static void on_seat_disable(struct libseat *seat, void *userdata);
static void on_seat_enable(struct libseat *seat, void *userdata);

static const struct libseat_seat_listener seat_listener = {
    .enable_seat = on_seat_enable,
    .disable_seat = on_seat_disable,
};

static void on_seat_enable(struct libseat *seat, void *userdata) {
    SwordfishCompositor *state = userdata;
    printf("libseat: Seat enabled. Attempting to open GPU device.\n");

    // Explicitly open the device now that the seat is active
    int device_id = libseat_open_device(seat, state->gpu_path, &state->gpu_fd);

    if (device_id < 0) {
        fprintf(stderr, "libseat_open_device failed for %s: %s\n", state->gpu_path, strerror(errno));
        state->gpu_fd = -1;
    } else {
        printf("libseat: Successfully opened GPU device FD %d (Device ID: %d)\n", state->gpu_fd, device_id);
        // Here you proceed with KMS/DRM/GBM initialization using state->gpu_fd
        state->seat_active = 1;
    }
}

static void on_seat_disable(struct libseat *seat, void *userdata) {
    SwordfishCompositor *state = userdata;
    printf("libseat: Seat disabled. Suspending compositor.\n");
    state->seat_active = 0;
    
    // Acknowledge the disable event *immediately* as required by the documentation
    if (libseat_disable_seat(seat) < 0) {
        fprintf(stderr, "libseat_disable_seat failed: %s\n", strerror(errno));
    }

    // You should close your devices here if necessary, or just stop using the FDs
    if (state->gpu_fd >= 0) {
        // libseat_close_device(seat, device_id); // Requires tracking device ID
        close(state->gpu_fd);
        state->gpu_fd = -1;
    }
}

void init_seat() {

  compositor.seat = libseat_open_seat(&seat_listener, &compositor);

  if (!compositor.seat) {
    fprintf(stderr, "Failed to open seat: %s\n", strerror(errno));
  }

  // Get the pollable FD for the libseat connection
  compositor.seat_fd = libseat_get_fd(compositor.seat);
  if (compositor.seat_fd < 0) {
    fprintf(stderr, "Failed to get libseat fd: %s\n", strerror(errno));
    libseat_close_seat(compositor.seat);
  }

  printf("libseat session opened successfully. Entering main loop.\n");

  pthread_t seat_thread_id;
  pthread_create(&seat_thread_id, NULL, run_seat_loop, NULL);
}

void check_libseat(){
  struct pollfd fds[] = {{compositor.seat_fd, POLLIN, 0}};
  // In a real compositor, you poll all input FDs, Wayland FD, and libseat FD
  poll(fds, 1, -1); // Wait indefinitely for events

  if (fds[0].revents & POLLIN) {
    // Dispatch pending libseat events, which triggers our callbacks
    if (libseat_dispatch(compositor.seat, 0) < 0) {
      fprintf(stderr, "libseat_dispatch failed: %s\n", strerror(errno));
    }
  }
}

void* run_seat_loop(void*none){
  while(1){
    check_libseat();
  }
}

