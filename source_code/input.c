#include "input.h"
#include <libinput.h>
#include <libudev.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include "swordfish.h"
#include "window.h"
#include <engine/time.h>
#include "keyboard.h"

#include <pway/pway.h>

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
    printf("Can't open udev\n");
  }

  libinput = libinput_udev_create_context(&interface, NULL, udev);
  if (!libinput) {
    // Handle error
    printf("Can't create libinput context\n");
  }

  libinput_udev_assign_seat(libinput, "seat0"); // Assign to a seat
}

void finish_input() {
  finish_keyboard();
  libinput_unref(libinput);
  udev_unref(udev);
  printf("finished input\n");
}

void *handle_input(void *none) {

  //as a wayland client the host compositor hands us input, and pway also has
  //to be pumped for the xdg surface to ever get configured. libinput here
  //would be reading the same devices a second time, behind the compositor's
  //back
  if (is_wayland_window) {
    printf("Input from pway\n");

    while (swordfish_running)
      pway_handle_events();

    return NULL;
  }

  printf("Input initialazing\n");

  init_input();
  init_keyboard();

  struct pollfd pfd = {
      .fd = libinput_get_fd(libinput),
      .events = POLLIN,
      .revents = 0,
  };

  while (poll(&pfd, 1, -1) > -1) {
    
    if(!swordfish_running)
      break;

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
        // Handle pointer motion event
        //printf("mouse movement\n");
        break;
      case LIBINPUT_EVENT_KEYBOARD_KEY:
        handle_xkb_keyboard_event(event);
        break;
        // ... other event types
      }

      libinput_event_destroy(event); // Free the event
    }
  }


  return NULL;
}
