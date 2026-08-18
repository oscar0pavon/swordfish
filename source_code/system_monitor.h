#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <pthread.h>

#include <engine/array.h>
#include <engine/model.h>
#include <engine/numbers.h>

#include "renderer/vk_buffer.h"

//no machine here has this many logical cpus, it just bounds the /proc/stat
//parse so a malformed file can't run off the end of the sample arrays
#define MONITOR_MAX_CORES 256

//one /proc/stat reading, in jiffies. usage is the delta between two of them
typedef struct CoreSample {
  u64 total;
  u64 idle;
} CoreSample;

//the machine's cpus drawn as a row of towers down the middle of the city
//street. one PInstance per logical core, height driven by utilisation, the
//whole set re-uploaded and drawn in a single instanced call
typedef struct SystemMonitor {
  PModel model;    //the same unit box the city uses
  Array towers;    //PInstance, one per logical cpu
  PBuffer instance_buffer;

  int core_count;

  //sampler thread only
  CoreSample previous[MONITOR_MAX_CORES];
  pthread_t sampler;
  bool running;

  //written by the sampler, read by the render loop
  pthread_mutex_t usage_mutex;
  float usage[MONITOR_MAX_CORES];

  //render loop only: usage eased over time so towers don't jump between
  //readings
  float displayed[MONITOR_MAX_CORES];
} SystemMonitor;

extern SystemMonitor system_monitor;

void system_monitor_init(SystemMonitor *monitor);

void system_monitor_draw(SystemMonitor *monitor, VkCommandBuffer *cmd_buffer,
                         u32 image_index);

void system_monitor_clean(SystemMonitor *monitor);

#endif
