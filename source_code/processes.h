#ifndef PROCESSES_H
#define PROCESSES_H

#include <pthread.h>

#include <engine/array.h>
#include <engine/model.h>
#include <engine/numbers.h>
#include <engine/vertex.h>

#include <engine/renderer/vk_buffer.h>

//array_add does not grow the array, so this is a hard limit on how many
//processes can be on screen at once
#define PROCESS_MAX 512

//one slot in the ring. the slot keeps its ring position for the whole run:
//when a process exits its slot is freed and the next new process reuses it,
//so nothing shuffles when something dies
typedef struct ProcessSlot {
  int pid;             //0 when free
  bool seen;           //set while a sweep is walking /proc
  u64 cpu_jiffies;     //utime+stime at the previous sample
  float cpu;           //fraction of one core
  float memory;        //resident set as a fraction of total ram
} ProcessSlot;

//the running processes drawn as a city ringing the cpu die. height is what a
//process is doing, colour is how much memory it is holding
typedef struct Processes {
  PModel model;    //the same unit box the city and the die use
  Array towers;    //PInstance, always PROCESS_MAX of them
  PBuffer instance_buffer;

  //sampler thread only
  ProcessSlot slots[PROCESS_MAX];
  u64 previous_total_jiffies;
  u64 total_memory_kb;
  int core_count;
  pthread_t sampler;
  bool running;

  //written by the sampler, read by the render loop
  pthread_mutex_t sample_mutex;
  float cpu[PROCESS_MAX];
  float memory[PROCESS_MAX];
  bool live[PROCESS_MAX];
  u32 name[PROCESS_MAX][PINSTANCE_NAME_WORDS];
  float name_length[PROCESS_MAX];

  //render loop only
  float displayed_cpu[PROCESS_MAX];
  float displayed_memory[PROCESS_MAX];
} Processes;

extern Processes processes;

void processes_init(Processes *target);

void processes_draw(Processes *target, VkCommandBuffer *cmd_buffer,
                    u32 image_index);

void processes_clean(Processes *target);

#endif
