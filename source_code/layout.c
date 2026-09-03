#include "layout.h"

#include "compositor.h"
#include "surface.h"
#include "top_level.h"
#include "outputs.h"
#include "sword.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "tasks.h"
#include "input.h"
#include "log.h"

//the space between two windows, in render target pixels. the whole point of it
//is that a tiled window has an edge - without one two terminals side by side
//read as a single wider terminal
#define LAYOUT_GAP 0

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

//is this surface a window the layout is responsible for placing. a floating
//window is still a window - it keeps the keyboard focus cycle, it still
//closes on super+c - it has just been pulled out of the grid, so every count
//and every cell assignment below has to skip it or it would be handed a tile
//out from under itself the next time anything else maps or closes
static bool task_is_tiled(Task *task){
  return task_is_window(task) && !task->is_floating;
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

//with no output_index filter, every window everywhere - used by the focus
//cycling below, which walks across outputs on purpose
static int count_windows(void){

  int count = 0;
  Task *task;

  wl_list_for_each(task, &compositor.surfaces, link){
    if(task_is_window(task))
      count++;
  }

  return count;
}

static int count_windows_on_output(int output_index){

  int count = 0;
  Task *task;

  wl_list_for_each(task, &compositor.surfaces, link){
    if(task_is_tiled(task) && task->output_index == output_index)
      count++;
  }

  return count;
}

//tiles each output's windows into that output's own area, in virtual desktop
//coordinates - draw_surface() subtracts the output's origin back out, and
//pointer_inside() compares against the cursor, which is in the same virtual
//space, so neither has to know outputs exist
static void layout_apply_output(int output_index){

  SwordOutput *out = &sword_outputs[output_index];

  int count = count_windows_on_output(output_index);

  if(count == 0)
    return;

  //the image this render target draws, which is also what its wl_output
  //advertises as its mode. a resize would come through here once the swap
  //chain can change size
  LayoutRect free = {out->x + LAYOUT_HALF_GAP, out->y + LAYOUT_HALF_GAP,
                     out->width - LAYOUT_GAP, out->height - LAYOUT_GAP};

  //surfaces are inserted at the head of the list, so walking it backwards is
  //map order: the oldest window on this output is the one holding the
  //biggest cell
  Task *task;
  int index = 0;

  wl_list_for_each_reverse(task, &compositor.surfaces, link){

    if(!task_is_tiled(task) || task->output_index != output_index)
      continue;

    //the last window is not split off anything, it takes whatever is left
    LayoutRect cell = (index == count - 1) ? free
                                           : layout_take_cell(&free, index);

    cell = layout_inset(cell);

    task->tile_x = cell.x;
    task->tile_y = cell.y;
    task->tile_width = cell.width;
    task->tile_height = cell.height;

    //a configure the client has already answered is one it would repaint for
    //nothing, and a relayout that reaches every window twice a second is how a
    //tiler ends up costing more than the scene it is drawn over
    if(task->top_level->width != cell.width ||
       task->top_level->height != cell.height)
      send_top_level_configure(task->top_level, cell.width, cell.height);

    index++;
  }
}

void layout_apply(void){

  for(int i = 0; i < sword_outputs_count; i++)
    layout_apply_output(i);
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

  int count = count_windows();

  if(count > 1){

    //nothing focused yet means starting at the first window rather than
    //nowhere
    int current = focused_window_index();
    int next = (current < 0) ? 0 : (current + direction + count) % count;

    Task *task = window_at_index(next);

    if(task){
      focused_task = task;
      //sword_frame_step()'s handle_focus() is what turns this into
      //wl_keyboard.leave, enter, and a fresh clipboard offer
      is_focus_completed = false;
      //deliberately not layout_raise() here: cycling and stacking read the
      //same list, so raising the window this just landed on would move it to
      //index count-1 and make the next super+j land on it again instead of
      //advancing - a float keeps the keyboard without needing to be on top
      log_info("Focus moved to window %i of %i", next + 1, count);
    }
  }
}

//a window going away takes the keyboard with it: forget_task() drops the
//pointer because the memory is about to be freed, and nothing else ever picks
//one up, so after super+c the next key went nowhere. hand the focus to a
//survivor instead
void layout_focus_fallback(void){

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

    //sword_frame_step()'s handle_focus() is what turns this into
    //wl_keyboard.leave, enter and a fresh clipboard offer
    is_focus_completed = false;

    if(next)
      log_info("Focus fell back to another window");
  }
}

void layout_close_focused(void){

  if(focused_task && focused_task->top_level){
    log_info("Closing focused window");
    top_level_close(focused_task->top_level);
  }

  //sent right away rather than waiting for the loop's own flush at the top of
  //its next iteration - the same reason send_wayland_key() flushes by hand
  wl_display_flush_clients(compositor.display);
}

//moves a floating window to the head of compositor.surfaces - the very same
//list draw_surfaces() and pointer_hit_task() walk to decide stacking order
//among floats, so raising is nothing more than relinking. a no-op on anything
//not floating: that list is also layout_apply_output()'s map order, and
//reordering it there would hand an unrelated tiled window a different cell
//the next time the layout runs
void layout_raise(Task *task){

  if(!task || !task->is_floating)
    return;

  wl_list_remove(&task->link);
  wl_list_insert(&compositor.surfaces, &task->link);
}

//a floating window is shrunk to this fraction of its output rather than kept
//at whatever size the tiling last gave it, so floating always looks like the
//floating window it is - a dialog sitting over the tiling, not a tile with
//nothing tiled around it
#define FLOAT_WIDTH_FRACTION 0.6
#define FLOAT_HEIGHT_FRACTION 0.6

//flip the focused window between tiled and floating. going floating gives it
//a centered rectangle sized off its output and raises it above everything
//else; coming back hands it to the layout again. either direction changes how
//many windows the tiling on this output has to divide the space between, so
//layout_apply() runs at the end regardless of which way it went
void layout_toggle_floating(void){

  Task *task = focused_task;

  if(!task || !task_is_window(task))
    return;

  task->is_floating = !task->is_floating;

  if(task->is_floating){

    SwordOutput *out = &sword_outputs[task->output_index];

    int32_t width = at_least(out->width * FLOAT_WIDTH_FRACTION, LAYOUT_MIN_SIZE);
    int32_t height = at_least(out->height * FLOAT_HEIGHT_FRACTION, LAYOUT_MIN_SIZE);

    task->tile_x = out->x + (out->width - width) / 2;
    task->tile_y = out->y + (out->height - height) / 2;

    //a configure the client already has is one it would repaint for nothing -
    //the same guard layout_apply_output() makes for a tiled resize
    if(task->top_level->width != width || task->top_level->height != height)
      send_top_level_configure(task->top_level, width, height);

    task->tile_width = width;
    task->tile_height = height;

    layout_raise(task);

    log_info("Window floated");

  }else{
    log_info("Window tiled");
  }

  layout_apply();
}

//there is no layer shell here, so a client that would be an overlay on another
//compositor has to be an ordinary xdg toplevel that sword recognises. the app
//id is the only thing a client says about what it is
#define LAUNCHER_APP_ID "pmenu"

//a strip across the top of the output, the shape dmenu and wmenu take. it is
//the compositor that decides this, exactly as it decides a tile, and the
//launcher draws whatever fits in the size it is configured at
#define LAUNCHER_HEIGHT 40

//the launcher is not given a cell: it covers the top of the output it opened
//on and sits over the tiling, which stays where it is - nothing here reserves
//space the way a layer surface's exclusive zone would.
//
//called from set_app_id(), the first moment sword knows what a client is. the
//initial configure for a tile has already gone out by then and this sends a
//second one, which costs nothing: the client has not drawn yet and acks the
//last one it was sent
void layout_place_launcher(Task *task, const char *app_id){

  if(!task || !task_is_window(task) || !app_id)
    return;

  if(strcmp(app_id, LAUNCHER_APP_ID) != 0)
    return;

  SwordOutput *out = &sword_outputs[task->output_index];

  int32_t height = at_least(LAUNCHER_HEIGHT, LAYOUT_MIN_SIZE);

  task->is_floating = true;
  task->tile_x = out->x;
  task->tile_y = out->y;
  task->tile_width = out->width;
  task->tile_height = height;

  if(task->top_level->width != out->width || task->top_level->height != height)
    send_top_level_configure(task->top_level, out->width, height);

  layout_raise(task);

  log_info("Launcher placed on output %i", task->output_index);

  //one window fewer for the tiling to divide the output between
  layout_apply();
}

//wl-clipboard falls back to an ordinary xdg_toplevel to grab the keyboard and
//claim the selection when neither data-control protocol is advertised (sword
//advertises neither - see CLAUDE.md's "Advertised global versions"). That
//window is never meant to be seen, but it still maps as a real tile and takes
//half the output until it exits, so every wl-copy/wl-paste briefly halves the
//layout and reflows it back a moment later
#define CLIPBOARD_HELPER_APP_ID "io.github.bugaevc.wl-clipboard"

void layout_hide_clipboard_helper(Task *task, const char *app_id){

  if(!task || !task_is_window(task) || !app_id)
    return;

  if(strcmp(app_id, CLIPBOARD_HELPER_APP_ID) != 0)
    return;

  task->is_floating = true;
  task->tile_x = -LAYOUT_MIN_SIZE;
  task->tile_y = -LAYOUT_MIN_SIZE;
  task->tile_width = 1;
  task->tile_height = 1;

  log_info("Clipboard helper window hidden");

  //one window fewer for the tiling to divide the output between
  layout_apply();
}
