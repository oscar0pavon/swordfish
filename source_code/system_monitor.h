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

//used, cached, free, swap
#define MONITOR_MEMORY_TOWERS 4

//one /proc/stat reading, in jiffies. usage is the delta between two of them
typedef struct CoreSample {
  u64 total;
  u64 idle;
} CoreSample;

//the machine drawn as towers down the middle of the city street: one per
//logical cpu, then a short block of memory towers ahead of them. they share
//one PInstance array so the whole set is still a single instanced draw call
typedef struct SystemMonitor {
  PModel model;    //the same unit box the city uses
  Array towers;    //PInstance, core_count cpus then MONITOR_MEMORY_TOWERS
  PBuffer instance_buffer;

  int core_count;

  //sampler thread only
  CoreSample previous[MONITOR_MAX_CORES];
  //which coretemp tempN_input belongs to each logical cpu, -1 if none
  int temperature_input[MONITOR_MAX_CORES];
  char coretemp_path[256];
  pthread_t sampler;
  bool running;

  //written by the sampler, read by the render loop
  pthread_mutex_t sample_mutex;
  float usage[MONITOR_MAX_CORES];
  float temperature[MONITOR_MAX_CORES];
  float memory[MONITOR_MEMORY_TOWERS];

  //render loop only: readings eased over time so towers don't jump between
  //samples
  float displayed_usage[MONITOR_MAX_CORES];
  float displayed_temperature[MONITOR_MAX_CORES];
  float displayed_memory[MONITOR_MEMORY_TOWERS];
} SystemMonitor;

extern SystemMonitor system_monitor;

void system_monitor_init(SystemMonitor *monitor);

void system_monitor_draw(SystemMonitor *monitor, VkCommandBuffer *cmd_buffer,
                         u32 image_index);

void system_monitor_clean(SystemMonitor *monitor);

#endif
