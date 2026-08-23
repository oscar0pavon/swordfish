#include "device_input.h"
#include <libinput.h>
#include <libudev.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <engine/time.h>
#include "keyboard.h"
#include "mouse.h"
#include "log.h"

LibInput* libinput;
struct udev *udev;

static int open_restricted(const char *path, int flags, void *user_data) {
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    close(fd);
}

const static struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

void init_input() {
  udev = udev_new();
  if (!udev) {
    // Handle error
    log_error("Can't open udev");
  }

  libinput = libinput_udev_create_context(&interface, NULL, udev);
  if (!libinput) {
    // Handle error
    log_error("Can't create libinput context");
  }

  libinput_udev_assign_seat(libinput, "seat0"); // Assign to a seat
}

void finish_input() {
  libinput_unref(libinput);
  udev_unref(udev);
  log_info("finished input");
}

//drains and dispatches whatever libinput already has queued. no poll of its
//own - the caller (the compositor thread's single poll loop) already knows
//the libinput fd is readable before calling this
void dispatch_libinput_events(void) {

  libinput_dispatch(libinput); // Process events

  struct libinput_event *event;

  while ((event = libinput_get_event(libinput))) {
    // Handle the event based on its type
    enum libinput_event_type type = libinput_event_get_type(event);

    switch (type) {
    case LIBINPUT_EVENT_DEVICE_ADDED:
      // Handle device added event
      break;
    case LIBINPUT_EVENT_POINTER_MOTION:
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
    case LIBINPUT_EVENT_POINTER_BUTTON:
    case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
    case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
    case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
      handle_libinput_pointer_event(event);
      break;
    case LIBINPUT_EVENT_KEYBOARD_KEY:
      handle_xkb_keyboard_event(event);
      break;
      // ... other event types
    }

    libinput_event_destroy(event); // Free the event
  }
}
