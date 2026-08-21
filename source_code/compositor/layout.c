#include "layout.h"

#include "compositor.h"
#include "surface.h"
#include "top_level.h"
#include "window.h"
#include "swordfish.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include "tasks.h"
#include "input.h"

//the space between two windows, in render target pixels. the whole point of it
//is that a tiled window has an edge - without one two terminals side by side
//read as a single wider terminal
#define LAYOUT_GAP 8

//every cell gives up half a gap on each side, so two neighbours between them
//give up a whole one. the output starts out short of the same half gap, which
//is what makes the margin at the edge of the screen the same width as the gap
//between two windows rather than half of it
#define LAYOUT_HALF_GAP (LAYOUT_GAP / 2)

//1: the free area rotates, so the windows wind inward the way dwm's spiral
//does. 0: dwindle, where the free area always walks toward the bottom right
//and every split is in the same two directions
#define LAYOUT_SPIRAL 1

typedef struct LayoutRect{
  int32_t x, y, width, height;
}LayoutRect;

//a cell narrower than this is not worth handing to a client - it would rather
//have a too-small window than a zero-sized one, which is a protocol error to
//configure
#define LAYOUT_MIN_SIZE 32

static int32_t at_least(int32_t value, int32_t minimum){
  return value < minimum ? minimum : value;
}

//is this surface a window, as opposed to a cursor image or a wl_surface the
//client has not made a toplevel out of yet
static bool task_is_window(Task *task){
  return task->top_level != NULL && !task->is_cursor;
}

//split what is left in two, hand one half to this window and keep the other.
//which half depends on the step: in spiral mode the direction cycles through
//right, down, left, up so the free area winds inward, in dwindle mode it only
//ever alternates between right and down
static LayoutRect layout_take_cell(LayoutRect *free, int step){

  LayoutRect cell = *free;

#if LAYOUT_SPIRAL
  int direction = step % 4;
#else
  int direction = step % 2;
#endif

  switch(direction){
  case 0://the window takes the left half
    cell.width = free->width / 2;
    free->x += cell.width;
    free->width -= cell.width;
    break;
  case 1://the top half
    cell.height = free->height / 2;
    free->y += cell.height;
    free->height -= cell.height;
    break;
  case 2://the right half, which is what turns the dwindle into a spiral
    cell.width = free->width / 2;
    cell.x = free->x + free->width - cell.width;
    free->width -= cell.width;
    break;
  default://the bottom half
    cell.height = free->height / 2;
    cell.y = free->y + free->height - cell.height;
    free->height -= cell.height;
    break;
  }

  return cell;
}

//the gap comes out of every cell rather than out of the free area, so a window
//that is split off later does not inherit a smaller share of it
static LayoutRect layout_inset(LayoutRect cell){

  cell.x += LAYOUT_HALF_GAP;
  cell.y += LAYOUT_HALF_GAP;
  cell.width = at_least(cell.width - LAYOUT_GAP, LAYOUT_MIN_SIZE);
  cell.height = at_least(cell.height - LAYOUT_GAP, LAYOUT_MIN_SIZE);

  return cell;
}

static int count_windows(void){

  int count = 0;
  Task *task;

  wl_list_for_each(task, &compositor.surfaces, link){
    if(task_is_window(task))
      count++;
  }

  return count;
}

void layout_apply(void){

  //this sends configures, so it takes the outermost lock first. it is
  //recursive and every caller so far is already inside it, on the compositor
  //thread
  lock_wayland();

  int count = count_windows();

  if(count == 0){
    unlock_wayland();
    return;
  }

  //the image swordfish renders, which is also what the wl_output advertises as
  //its mode. a resize would come through here once the swap chain can change
  //size
  LayoutRect free = {LAYOUT_HALF_GAP, LAYOUT_HALF_GAP,
                     WINDOW_WIDTH - LAYOUT_GAP, WINDOW_HEIGHT - LAYOUT_GAP};

  //surfaces are inserted at the head of the list, so walking it backwards is
  //map order: the oldest window is the one holding the biggest cell
  Task *task;
  int index = 0;

  wl_list_for_each_reverse(task, &compositor.surfaces, link){

    if(!task_is_window(task))
      continue;

    //the last window is not split off anything, it takes whatever is left
    LayoutRect cell = (index == count - 1) ? free
                                           : layout_take_cell(&free, index);

    cell = layout_inset(cell);

    //draw_surface() reads these on the render thread while this runs on the
    //compositor thread. lock_wayland() is already held, and this is the inner
    //lock of the two
    pthread_mutex_lock(&draw_tasks_mutex);
    task->tile_x = cell.x;
    task->tile_y = cell.y;
    task->tile_width = cell.width;
    task->tile_height = cell.height;
    pthread_mutex_unlock(&draw_tasks_mutex);

    //a configure the client has already answered is one it would repaint for
    //nothing, and a relayout that reaches every window twice a second is how a
    //tiler ends up costing more than the scene it is drawn over
    if(task->top_level->width != cell.width ||
       task->top_level->height != cell.height)
      send_top_level_configure(task->top_level, cell.width, cell.height);

    index++;
  }

  unlock_wayland();
}

//the window the focus is on, as an index into the layout order, or -1
static int focused_window_index(void){

  Task *task;
  int index = 0;

  wl_list_for_each_reverse(task, &compositor.surfaces, link){

    if(!task_is_window(task))
      continue;

    if(task == focused_task)
      return index;

    index++;
  }

  return -1;
}

static Task *window_at_index(int wanted){

  Task *task;
  int index = 0;

  wl_list_for_each_reverse(task, &compositor.surfaces, link){

    if(!task_is_window(task))
      continue;

    if(index == wanted)
      return task;

    index++;
  }

  return NULL;
}

void layout_focus_next(int direction){

  //the input thread, so the wayland lock comes first and focus_task_mutex
  //inside it - the same order handle_focus() takes them in
  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  int count = count_windows();

  if(count > 1){

    //nothing focused yet means starting at the first window rather than
    //nowhere
    int current = focused_window_index();
    int next = (current < 0) ? 0 : (current + direction + count) % count;

    Task *task = window_at_index(next);

    if(task){
      focused_task = task;
      //handle_focus() on the render thread is what turns this into
      //wl_keyboard.leave, enter, and a fresh clipboard offer
      is_focus_completed = false;
      printf("Focus moved to window %i of %i\n", next + 1, count);
    }
  }

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

//a window going away takes the keyboard with it: forget_task() drops the
//pointer because the memory is about to be freed, and nothing else ever picks
//one up, so after super+c the next key went nowhere. hand the focus to a
//survivor instead
void layout_focus_fallback(void){

  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  //still on a live window means the one that closed was not the focused one,
  //and moving the focus off it would be the bug rather than the fix.
  //task_is_window() is what catches a surface whose xdg_toplevel is gone but
  //whose wl_surface is not - it is no longer a window, so it cannot hold the
  //keyboard
  if(!focused_task || !task_is_window(focused_task)){

    Task *task;
    Task *next = NULL;

    //surfaces are inserted at the head, so this walks newest first: the focus
    //lands on the window the closed one was mapped on top of rather than on
    //the oldest one on screen
    wl_list_for_each(task, &compositor.surfaces, link){

      if(!task_is_window(task))
        continue;

      next = task;
      break;
    }

    focused_task = next;

    //handle_focus() on the render thread is what turns this into
    //wl_keyboard.leave, enter and a fresh clipboard offer
    is_focus_completed = false;

    if(next)
      printf("Focus fell back to another window\n");
  }

  pthread_mutex_unlock(&focus_task_mutex);
  unlock_wayland();
}

void layout_close_focused(void){

  lock_wayland();
  pthread_mutex_lock(&focus_task_mutex);

  if(focused_task && focused_task->top_level){
    printf("Closing focused window\n");
    top_level_close(focused_task->top_level);
  }

  pthread_mutex_unlock(&focus_task_mutex);

  //the input thread is not the one pumping the event loop, so nothing reaches
  //the client until somebody flushes - the same reason send_wayland_key()
  //flushes by hand
  wl_display_flush_clients(compositor.display);

  unlock_wayland();
}
