#include "timers.h"

#include <sys/timerfd.h>

//how often the render loop is stepped
#define FRAME_INTERVAL_NS 16667000L

int frame_timer_fd = 0;

void init_frame_timer(void){

  frame_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  struct itimerspec frame_interval = {
      .it_interval = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
      .it_value = {.tv_sec = 0, .tv_nsec = FRAME_INTERVAL_NS},
  };
  timerfd_settime(frame_timer_fd, 0, &frame_interval, NULL);

}

