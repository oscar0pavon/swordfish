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
#include "tty.h"
#include <pthread.h>
#include <xf86drm.h>
#include "log.h"


static void on_seat_disable(struct libseat *seat, void *userdata);
static void on_seat_enable(struct libseat *seat, void *userdata);

static const struct libseat_seat_listener seat_listener = {
    .enable_seat = on_seat_enable,
    .disable_seat = on_seat_disable,
};

static void on_seat_enable(struct libseat *seat, void *userdata) {
  SwordfishCompositor *state = userdata;


}

static void on_seat_disable(struct libseat *seat, void *userdata) {
    SwordfishCompositor *state = userdata;
    log_info("libseat: Seat disabled. Suspending compositor.");
    state->seat_active = 0;
    
    // Acknowledge the disable event *immediately* as required by the documentation
    if (libseat_disable_seat(seat) < 0) {
        log_error("libseat_disable_seat failed: %s", strerror(errno));
    }

    // You should close your devices here if necessary, or just stop using the FDs
    if (state->gpu_fd >= 0) {
        // libseat_close_device(seat, device_id); // Requires tracking device ID
        close(state->gpu_fd);
        state->gpu_fd = -1;
    }
}

void init_seat() {
  log_info("Starting seat");

  compositor.seat = libseat_open_seat(&seat_listener, &compositor);

  if (!compositor.seat) {
    log_error("Failed to open seat: %s", strerror(errno));
  }


  // pthread_t seat_thread_id;
  // pthread_create(&seat_thread_id, NULL, run_seat_loop, NULL);


  log_info("libseat: Seat enabled. Attempting to open GPU device.");

  int device_id = libseat_open_device(compositor.seat, compositor.gpu_path,
                                      &compositor.gpu_fd);

  if (device_id < 0) {
    log_error("libseat_open_device failed for %s: %s",
              compositor.gpu_path, strerror(errno));
    compositor.gpu_fd = -1;
  } else {
    log_info("libseat: Successfully opened GPU device FD %d (Device ID: %d)",
             compositor.gpu_fd, device_id);
    compositor.seat_active = 1;
  }

  if (drmSetMaster(compositor.gpu_fd) < 0) {
    log_warn("Can't be DRM master");
  }

  // tty_save_state();

}

void check_libseat(){
  // In a real compositor, you poll all input FDs, Wayland FD, and libseat FD

}

void* run_seat_loop(void*none){
  log_info("libseat session opened successfully. Entering main loop.");
  int seat_fd = libseat_get_fd(compositor.seat);
  struct pollfd fds[] = {{seat_fd, POLLIN, 0}};
  while(1){
    log_debug("seat event loop");
    if (poll(fds, 1, -1) == -1) {
      if (errno == EINTR)
        continue; // Handle signals
      log_error("poll failed: %s", strerror(errno));
      break; // Exit loop on error
    }
  
    log_debug("have a event");
    if (fds[0].revents & POLLIN) {
      // Dispatch pending libseat events, which triggers our callbacks
      if (libseat_dispatch(compositor.seat, 0) < 0) {
        log_error("libseat_dispatch failed: %s", strerror(errno));
      }
      log_debug("dispatch");
    }
  }
}

