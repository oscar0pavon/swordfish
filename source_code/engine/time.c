#include "time.h"
#include <time.h>
#include <stdio.h>
#include <unistd.h> 

#define NSEC_PER_SEC 1000000000L
#define TARGET_FPS 30.0f
#define TARGET_FRAME_TIME_NSEC (NSEC_PER_SEC / TARGET_FPS)

static struct timespec frame_start_time;

void start_frame_timer() {
    clock_gettime(CLOCK_MONOTONIC, &frame_start_time);
}

void delay_for_frame() {
    struct timespec frame_end_time;
    clock_gettime(CLOCK_MONOTONIC, &frame_end_time);

    long elapsed_nsec = (frame_end_time.tv_sec - frame_start_time.tv_sec) * NSEC_PER_SEC +
                       (frame_end_time.tv_nsec - frame_start_time.tv_nsec);

    if (elapsed_nsec < TARGET_FRAME_TIME_NSEC) {
        struct timespec sleep_time;
        long remaining_nsec = TARGET_FRAME_TIME_NSEC - elapsed_nsec;

        sleep_time.tv_sec = remaining_nsec / NSEC_PER_SEC;
        sleep_time.tv_nsec = remaining_nsec % NSEC_PER_SEC;

        nanosleep(&sleep_time, NULL);
    }
}
