#include "draw.h"

#include <stdint.h>
#include <stdio.h>

#include "compositor.h"
#include "retire.h"
#include "shared_memory.h"
#include "subcompositor.h"
#include "surface.h"
#include "mouse.h"
#include "pointer.h"
#include "outputs.h"
#include "input.h"
#include "log.h"
#include "top_level.h"
#include "cursor.h"

#include <engine/renderer/sync.h>
#include <engine/array.h>
#include <engine/model.h>

#include <cglm/cglm.h>
#include <engine/renderer/pipeline.h>
#include <engine/engine2d.h>
#include <vulkan/vulkan_core.h>

#include <cglm/cglm.h>

#include <engine/renderer/uniform_buffer.h>

#include <engine/renderer/descriptor_set.h>
#include <engine/renderer/draw.h>
#include <engine/renderer/vk_images.h>
#include <engine/renderer/vulkan.h>
#include <engine/renderer/render_thread.h>
#include <engine/model.h>
#include <engine/time.h>

#include <wayland-server-core.h>

bool can_draw_surfaces = true;

//derivation (mount rotated 90 degrees counter-clockwise, so the panel's
//native right edge is what now points visually up - see outputs.h's
//SWORD_OUTPUT_ROTATE): a point (x, y) in this output's logical space maps to
//physical render-target pixels (Wp - y, x), where Wp is the render target's
//own physical width - which is out->height, since a 90 degree rotation is
//exactly what swaps them (sword_outputs_init() is where that swap happens).
//applying that same map to a rect's two edges rather than a single point
//swaps which local quad axis becomes which physical one: local x (the rect's
//logical width) becomes a vertical physical extent, local y (the rect's
//logical height) becomes a horizontal one - so this is a real rotation of
//the quad, not just a repositioning of it, and the client's buffer content
//comes out rotated with it. flip the signs below to get the other direction
//(mounted clockwise) if this reads upside-down on the hardware
void sword_draw_rotated(PModel *model, PRenderTarget *render_target,
                        SwordOutput *out, u32 image_index, vec2 position,
                        vec2 size) {

  //the projection has to be the one pengine builds, not one built here.
  //pengine compiles with CGLM_FORCE_DEPTH_ZERO_TO_ONE and
  //CGLM_FORCE_LEFT_HANDED and sword's own objects deliberately do not (see
  //CLAUDE.md), so the same glm_ortho() call means two different matrices
  //depending on which side of the library it is compiled on. calling it here
  //produced a right-handed -1..1 depth range, which puts the quads' own z -
  //0.5 for a window, 0.2 for the cursor - outside Vulkan's valid depth and
  //clips every one of them away: correct geometry, nothing on screen. so let
  //pe_2d_draw_on_target() set up the whole uniform block as usual, and only
  //replace the model matrix afterwards
  pe_2d_draw_on_target(model, render_target, image_index, position, size);

  float x = position[0], y = position[1];
  float w = size[0], h = size[1];

  mat4 model_matrix;
  glm_mat4_identity(model_matrix);

  model_matrix[0][0] = 0.0f;
  model_matrix[0][1] = w;
  model_matrix[1][0] = -h;
  model_matrix[1][1] = 0.0f;
  model_matrix[3][0] = (float)out->height - y;
  model_matrix[3][1] = x;

  glm_mat4_copy(model_matrix, model->uniform_buffer_object.model);

  //a second send, over the one pe_2d_draw_on_target() just did: it is a
  //memcpy into a mapped uniform buffer, and paying it twice is cheaper than
  //teaching sword to spell pengine's depth conventions itself
  pe_vk_send_uniform_buffer(model, image_index);
}

void draw_surface(Task* surface, PRenderTarget *render_target,
                  VkCommandBuffer *cmd_buffer, uint32_t index){

  SwordOutput *out =
      &sword_outputs[render_target - pe_render_targets];

  //where this surface goes on the virtual desktop: the cell the layout gave
  //the window, or - for a subsurface - its parent's rectangle plus its own
  //offset. task_screen_rect() (subcompositor.c) is the one place that knows
  //which, and pointer.c divides the cursor back through the same arithmetic.
  //the output's own origin comes back out here since this target only draws
  //its own slice of the desktop. a window's buffer is drawn at its own size
  //while it fits the cell and stretched into it while it does not - see
  //task_origin_and_scale()
  double x, y, width, height;

  if (!task_screen_rect(surface, &x, &y, &width, &height))
    return;

  vec2 position = {x - out->x, y - out->y};
  vec2 size = {width, height};

  //a window whose buffer is not the size of its cell, which is the frame or
  //two between a configure and the client repainting. it is drawn at its own
  //size when it fits and stretched when it does not
  //(task_origin_and_scale(), subcompositor.c), and the log says which - a
  //pair that never goes away is a client that never took the resize
  if (surface->top_level && surface->image && surface->tile_width > 0 &&
      (surface->image->width != surface->tile_width ||
       surface->image->heigth != surface->tile_height) &&
      (surface->image->width != surface->logged_image_width ||
       surface->image->heigth != surface->logged_image_height ||
       surface->tile_width != surface->logged_tile_width ||
       surface->tile_height != surface->logged_tile_height)) {

    surface->logged_image_width = surface->image->width;
    surface->logged_image_height = surface->image->heigth;
    surface->logged_tile_width = surface->tile_width;
    surface->logged_tile_height = surface->tile_height;

    DesktopSurface *desktop_surface = surface->top_level->surface;

    log_info("Drawing buffer %ix%i (window %ix%i at %i %i) in cell %ix%i (%s) "
             "for \"%s\"",
             surface->image->width, surface->image->heigth,
             desktop_surface ? desktop_surface->geometry_width : 0,
             desktop_surface ? desktop_surface->geometry_height : 0,
             desktop_surface ? desktop_surface->geometry_x : 0,
             desktop_surface ? desktop_surface->geometry_y : 0,
             surface->tile_width, surface->tile_height,
             width == surface->image->width ? "own size" : "stretched",
             surface->top_level->title ? surface->top_level->title
                                       : "(no title)");
  }

  //a menu is the one surface whose placement is worked out rather than handed
  //to it by the layout, so it is the one worth being able to see go wrong
  if (surface->popup_resource)
    log_debug("Drawing popup at %.0f %.0f, %.0fx%.0f, parent %p", x, y, width,
              height, (void *)surface->parent);

  if (out->rotated)
    sword_draw_rotated(&surface->model, render_target, out, index, position,
                       size);
  else
    pe_2d_draw_on_target(&surface->model, render_target, index, position,
                         size);

  pe_vk_descriptor_with_image_update(&surface->model, &main_render_target);//TODO

  PDrawModelCommand draw = {
    .model = &surface->model,
    .command_buffer = *cmd_buffer,
    .image_index = index,
    .layout = pe_vk_pipeline_layout3
  };
  pe_vk_draw_model(&draw);

}

//the shm clients' pixels, copied in before anything samples them. this used to
//sit at the end of the frame, after pe_frame_draw() had already recorded a
//quad reading the image - so what an shm client committed was never on screen
//until the frame after, and every redraw showed the previous one first. dmabuf
//never paid that because its quad samples the client's pages directly; shm is
//a copy, and the copy has to happen on this side of the draw
void begin_frame() {

  //an shm upload rewrites an image a frame in flight may still be sampling,
  //and the single-time commands it is made of carry no barrier against that.
  //pe_vk_draw_frame() used to end in vkQueueWaitIdle() and made every frame
  //safe for free; now the wait is paid explicitly, and only on the frames
  //where an shm client actually redrew
  bool any_upload = false;
  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *surface = array_get_pointer(&tasks_for_draw, i);
    if (surface->client_buffer &&
        surface->client_buffer->type == CLIENT_BUFFER_SHARED_MEMORY &&
        surface->client_buffer->needs_upload) {
      any_upload = true;
      break;
    }
  }

  if (!any_upload)
    return;

  //INFO the fences moved onto PRenderTarget with multimonitor - wait on every
  //target's, since an shm upload has no way to know which one last drew the
  //surface it is about to overwrite
  for (u32 t = 0; t < pe_render_targets_count; t++)
    vkWaitForFences(vk_device, PE_VK_FRAMES_IN_FLIGHT,
                    pe_render_targets[t].fence_in_flight, VK_TRUE, UINT64_MAX);

  //nothing is recording a command buffer here either: the previous frame's
  //recording ended in pe_frame_draw() and this one has not started
  for (int i = 0; i < tasks_for_draw.count; i++)
    task_upload_shared_memory(array_get_pointer(&tasks_for_draw, i));
}

void end_frame() {

  //vulkan objects whose wl_buffer the client destroyed while a frame was
  //still using them. retire.c counts frames in flight, so only what no frame
  //can still be reading is destroyed here
  retire_collect();

  //every surface, not only the ones in the draw list: a client that asks for a
  //frame callback before it has ever attached a buffer is waiting on that
  //callback to draw its first one, and a parent surface with all its content in
  //a subsurface may never attach one at all. answering only what was drawn left
  //both of them waiting forever
  Task *surface;
  wl_list_for_each(surface, &compositor.surfaces, link) {
    if (surface->frame_call_resource != NULL)
      send_frame_callback_done(surface);
  }

  for (int i = 0; i < tasks_for_draw.count; i++) {
    Task *surface = array_get_pointer(&tasks_for_draw, i);

    //any buffer the client replaced goes back once the gpu is provably past
    //the last frame that sampled it - task_release_old_buffer() counts the
    //frames itself now that pe_vk_draw_frame() no longer drains the queue
    task_release_old_buffer(surface);
  }

  //the tree as it stands at drawing time, a few frames after a menu appeared -
  //which is after begin_frame()'s shm upload has had its chance to run
  if (surface_tree_dump_countdown > 0 && --surface_tree_dump_countdown == 0)
    log_surface_tree("frames after the popup appeared");

  wl_display_flush_clients(compositor.display);
}

//a window and everything the client hung underneath it, parent first so the
//children land on top. the list is kept back to front, which is the order
//wl_subsurface.place_above/below maintain
static void draw_surface_tree(Task *task, PRenderTarget *render_target,
                              VkCommandBuffer *command, uint32_t index) {

  //a surface with no buffer draws nothing and still has children that do -
  //firefox's toplevel is exactly that, an empty parent with its whole
  //rendering container in a subsurface over it
  if (task->can_draw && task->image)
    draw_surface(task, render_target, command, index);

  Task *child;
  wl_list_for_each(child, &task->children, parent_link)
      draw_surface_tree(child, render_target, command, index);
}

void draw_surfaces(PRenderTarget *render_target, VkCommandBuffer *command,
                   uint32_t index) {

  int output_index = render_target - pe_render_targets;

  //walked over compositor.surfaces rather than tasks_for_draw, because a
  //surface only enters that array when it attaches a buffer and a parent that
  //never attaches one would take its children out of the scene with it.
  //surfaces go in at the head, so backwards is map order - the same order the
  //layout walks in, which for cells that cannot overlap is any order at all
  Task *task;

  //two passes so every floating window is drawn over every tiled one no
  //matter where the two land in map order - the first pass paints the tiles,
  //the second the floats. within a pass this is still just map order, which
  //is why layout_raise() moving a float toward the head of the list (visited
  //last by the reverse walk below) is what makes a raised float draw on top
  //of the rest
  for (int floating_pass = 0; floating_pass <= 1; floating_pass++) {

    wl_list_for_each_reverse(task, &compositor.surfaces, link) {

      //a child is drawn by its parent, at the parent's position
      if (task->parent || task->is_cursor)
        continue;

      //and a surface with no role at all is drawn by nobody. it has no cell,
      //because only a window gets one, so drawing it anyway put it at the
      //corner of the screen at its own size - which is where firefox's
      //tooltips ended up: the popup is dismissed, the client drops the
      //xdg_popup role and keeps the wl_surface mapped with its buffer still
      //on it, and what was a menu a moment ago becomes a rectangle stuck at
      //0,0. an unroled surface is not something the protocol says to show
      if (!task->top_level)
        continue;

      if (task->output_index != output_index)
        continue;

      if (task->is_floating != (bool)floating_pass)
        continue;

      draw_surface_tree(task, render_target, command, index);

      //no release goes out here. a dmabuf buffer is sampled straight out of
      //the client's memory, and this quad goes on sampling this one every
      //frame until the client attaches another - so telling the client it is
      //free the first time it is drawn hands back a buffer that is still on
      //screen. the client then picks it as its next render target and the
      //frame that lands mid-repaint shows the clear rather than the content.
      //task_release_old_buffer() in end_frame() is where it is answered
      //instead
    }
  }
}

//the render loop's step, folded into run_compositor()'s poll loop (compositor.c)
//via a timerfd instead of a separate thread with its own usleep(16667). now
//that drawing, wayland dispatch and input are all on the one thread, nothing
//in end_frame()/draw_surfaces() needs a lock any more
void sword_frame_step(void) {
  handle_focus();

  //the surface under the cursor, against a scene that may have changed since
  //the last mouse movement - a menu that just mapped over it, most of all
  pointer_refresh_focus();

  //the shm clients' pixels go on the gpu before the frame that samples them,
  //not after it
  begin_frame();

  pe_frame_draw();
  update_delta_time();
  end_frame();
}

//the 3D scene this used to draw - the cpu die, the process ring and the hud -
//is its own program now: 3dtop, an ordinary wayland client. it was never
//visible from behind the tiling anyway, and as a client it gets composited
//like anything else. what is left here is the compositor's own drawing
void sword_draw_scene(PRenderTarget *target, VkCommandBuffer *cmd_buffer, uint32_t index){

  int output_index = target - pe_render_targets;

  draw_surfaces(target, cmd_buffer, index);

  //the pointer over the windows, on whichever output it is actually over. no
  //client has ever handed over a cursor image - set_cursor keeps the surface
  //out of the draw list - so this arrow is the only pointer there is
  if (sword_output_index_at(cursor_x) == output_index)
    cursor_draw(&cursor, target, cmd_buffer, index);
}
