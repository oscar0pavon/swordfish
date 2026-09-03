#include "sword.h"
#include "cursor.h"

#include <engine/engine2d.h>

#include <signal.h>
#include <engine/memory.h>
#include <engine/camera.h>

#include "log.h"
#include "surface.h"
#include "keyboard.h"
#include "tty.h"
#include "outputs.h"
#include "draw.h"

void clean_sword(){

  cursor_clean(&cursor);

}

void sword_init(){

  log_init();

  signal(SIGINT, handle_signal);
  //pkill's default. without it a kill leaves the tty in graphics mode with
  //VT_PROCESS still set, and no VT can be switched to afterwards
  signal(SIGTERM, handle_signal);

  //INFO measured arena usage under normal load is ~15KB (Arrays and other
  //bookkeeping structs only - client buffers and textures go through Vulkan,
  //not this arena). 16MB leaves generous headroom without the old 750MB's
  //memset committing that much RSS on every startup
  pe_init_memory(16 * 1024 * 1024);
  
  array_init(&tasks_for_draw, sizeof(void *), 50);

  init_keyboard();

  pe_vk_validation_layer_enable = true;

  is_drm_rendering = true;

  //the console the printf() calls all over sword are writing to is about to
  //be under the frames we draw
  log_redirect_stdio();

  compositor.gpu_path = "/dev/dri/card0";

  //INFO before pe_vk_init(), and that ordering is the whole trick: this
  //takes DRM master, which makes the fd radv opens for itself non-master,
  //which leaves mesa's wsi_display with no fd of its own - so the hook below
  //can install ours instead and sword can drop the display again when
  //the VT is switched away. taking master after vulkan is up is too late
  if (!tty_session_init(compositor.gpu_path))
    log_warn("No VT session: switching away will not release the display");

  pe_vk_acquire_display = sword_acquire_drm_display;
  pe_vk_sort_displays = sword_sort_displays_by_connector;

  pe_vk_draw_scene = sword_draw_scene;

  pe_vk_init();

  sword_outputs_init();

  sword_capture_display_routing();
  sword_log_display_routing("startup");

  camera_init(&main_camera);
  //fills in the ortho projection cursor_init() copies, so it comes first
  pe_2d_init();

  cursor_init(&cursor);

}
