#ifndef TOP_LEVEL_H
#define TOP_LEVEL_H

#include "compositor.h"
#include "desktop.h"

//in the header because the layout has to reach the size a window was last
//configured at, and the Task carries a pointer back to it
typedef struct TopLevel{
  DesktopSurface *surface;
  WResource *resource;
  char *title;
  char *app_id;
  //what the client was last configured at, so a state request can be answered
  //with the size it already has - and so the layout can tell whether it owes
  //this window a new configure at all
  int32_t width;
  int32_t height;
  //the client's own limits. recorded because the protocol calls them state,
  //not because anything sizes a window from them yet
  int32_t min_width;
  int32_t min_height;
  int32_t max_width;
  int32_t max_height;
}TopLevel;

void get_top_level_implementation(WClient *client,
                                  WResource *resource, uint32_t id);

//configure the window at this size and take a serial for the ack. the layout
//is what calls this now
void send_top_level_configure(TopLevel *toplevel, int width, int height);

//ask the client to close. it is a request, not an order - the client is free
//to put up a "save your work?" dialog and stay
void top_level_close(TopLevel *top_level);

//the task is going away first, so the listener living inside it has to come
//out of the toplevel's destroy signal before the memory does
void task_stop_listening_to_top_level(Task *task);

#endif
