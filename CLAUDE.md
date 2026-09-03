# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Sword is a **Wayland compositor** with a **Vulkan renderer**: a tiling
window manager that draws every client as a textured quad. The renderer and the
engine are not in this repo — they are **pengine** (`/root/pengine`), linked in
as `libpengine.a`. What is left here is the compositor.

It used to be more than that. Sword began as one binary that was also a 3D
engine, displaying software build progress as a 3D scene inspired by the movie
*Sword*, with client windows composited into that world. On 2026-08-23 the
scene moved out to **3dtop** (`/root/3dtop`), a standalone 3D system monitor that
is now an ordinary Wayland client. The reason was simple: the tiling layout
covers the whole output, so the 3D world was never actually visible behind the
windows. The build-output half (`build.c`'s `call_make`) went with it; only
`launch_program()` survived, in `launch.c`, because keybindings still spawn a
terminal, a browser and the launcher.

**The movie premise is gone, deliberately.** Compositing client windows *into* a
3D world only worked while one binary owned both. Do not reintroduce the scene
here — 3dtop is a client like any other now, and is sword's first-party test
client for the dmabuf import path, the multimonitor layout, and rotation.

**This is daily-driver software, not a demo.** It runs on a tty as the author's
working compositor. Firefox, Thunar, GIMP and pwvucontrol all run under it —
four independent toolkits, with working menus, clipboard and pointer input —
alongside the first-party pterminal, pmenu and 3dtop. Treat a regression here
as breaking someone's desktop, because it does.

What is genuinely missing, in the order it is felt: **XWayland** (no X11 client
runs at all), **workspaces** (one spiral per output and an unmap keeps its
cell), **resize / mode change / monitor hotplug**, `wlr_layer_shell`, and
**damage tracking** (every frame is a full redraw at ~60Hz). Note also that
nothing here starts a D-Bus session bus, so clients that want one degrade —
Thunar loses thumbnails and xfconf settings, and says so in the log.

## Build / install

The build no longer hardcodes the repo path: `source_code/Makefile` uses `-I$(CURDIR)` and `generate_compile_commands.sh` writes the actual working directory into `compile_commands.json`'s `directory` field, so the repo can live anywhere. pengine and pway still install under the fixed system prefixes `/usr/local/include` and `/usr/local/lib` (see **pengine** below), same as any other library.

```sh
make                  # root Makefile: builds AND runs `make install` (needs root)
make -C source_code   # build only -> ./sword
make clean            # removes binary, *.o, ../shaders/*.spv
./source_code/generate_compile_commands.sh   # regenerate compile_commands.json for clangd
```

`make install` copies the binary to `$(PREFIX)/bin` and the shaders and images
to `/usr/libexec/sword/shaders` and `/usr/libexec/sword/images`. **Assets are
loaded at runtime from absolute `/usr/libexec/sword/...` paths** — the two
shader pairs in `surface.c` and `cursor.c` are the only ones left — so a shader
change needs a reinstall before it has any effect. The subdirectory matters:
`install` used to be `cp -r shaders /usr/libexec/sword`, which on a machine
where that directory does not exist yet creates it *as* a copy of `shaders`,
and the stray `.spv` files still sitting at the top of `/usr/libexec/sword` on
an old install are from exactly that.

Shaders are GLSL compiled with `glslc` into `shaders/*.spv`. That directory is
gitignored, so it does not exist on a fresh clone and the rules create it
through an order-only prerequisite.

There is no test suite and no linter.

### pengine

`engine/` and `renderer/` used to be subdirectories here. They are now
`/root/pengine/src/engine` and `/root/pengine/src/engine/renderer`, built into
`/usr/local/lib/libpengine.a` by `make && make install` in that repo. **A change
to anything under `renderer/` or `engine/` is a change to pengine**, and needs a
rebuild and reinstall there before sword sees it.

`make install` in pengine copies the header tree to `/usr/local/include/pengine`
keeping the `engine/...` prefix it is written against, so sword compiles
with the single `-I/usr/local/include/pengine` and keeps spelling its includes
`<engine/model.h>` and `<engine/renderer/vulkan.h>`. The `"renderer/..."`
includes *inside* pengine's own headers resolve relative to the header doing the
including, which is why no second `-I` is needed - and why adding one is a bad
idea: it would put pengine's `engine/time.h` and `engine/input.h` ahead of
libc's `<time.h>`.

Two flags in `source_code/Makefile` are there because of pengine and are not
optional:

- **`-fcommon`**. pengine's headers hold about a hundred tentative definitions
  (`bool thread_main;` in `threads.h`, `struct Input input;` in `input.h`), so
  every object that includes one defines it. They merge under `-fcommon` and
  collide at link time without it. pengine builds with it too.
- **`-Wno-incompatible-pointer-types`**. `main_camera` is pengine's, and it is a
  `CameraComponent` - a `Camera` plus one trailing pointer - while
  `camera_init()` and `pe_camera_look_at()` take `Camera *`.

`-DCGLM_FORCE_DEPTH_ZERO_TO_ONE -DCGLM_FORCE_LEFT_HANDED` moved with the code
into pengine's `include.make`. They apply to pengine only; sword's own
objects are built without them, exactly as before. Keep cglm math on pengine's
side so the depth and handedness conventions stay consistent.

The shm upload (`shared_memory.c`) assembles its own version of
`pe_vk_create_texture_from_image()` out of `pe_vk_transition_image_layout()`,
`pe_vk_image_copy_buffer()` and `pe_vk_create_texture_sampler()`, because that
function hardcodes `VK_FORMAT_R8G8B8A8_SRGB` and takes a `PImage`. The three were
already external; they only had to be **declared** in pengine's `vk_images.h`.
All of them end in `vkQueueWaitIdle` — see **Frame path** for where they may be
called from.

### What sword hands the renderer

pengine cannot call `sword_draw_scene()` by name any more, and three globals
that used to be sword's now belong to the renderer. `main()` wires them up
before `pe_vk_init()`:

- `pe_vk_draw_scene` - a function pointer, set to `sword_draw_scene`.
  `pe_vk_draw_commands()` calls it in the middle of the render pass.
- `pe_window_width` / `pe_window_height` - **not** set here any more. sword used
  to assign a pair of compile-time constants to them before `pe_vk_init()`;
  `pe_vk_init()` overwrites both from `pe_render_targets[0]` once the swap
  chains exist, so the assignment never survived to be read. The camera and the
  2D ortho projection are initialised after `pe_vk_init()` and get the display's
  real mode. `swap_chain.c` only reads them on the non-DRM path.
- `is_drm_rendering` is declared in `<engine/renderer/renderer.h>` and defined
  in pengine's `vulkan.c`. `main()` sets it to `true` unconditionally.
- `pe_vk_acquire_display` and `pe_vk_sort_displays` - function pointers, set to
  `sword_acquire_drm_display` and `sword_sort_displays_by_connector`
  (`outputs.c`). The first is how sword hands the renderer the DRM display it
  took master on rather than letting mesa open its own; the second is what puts
  the monitors in connector order. Both are wired before `pe_vk_init()` for the
  same reason everything else here is.

### Generated Wayland protocol code

`desktop-server.{c,h}` (xdg-shell) and `linux-dmabuf.{c,h}` are `wayland-scanner` output but are checked into git. Regenerate with `generate_wayland_protocol_files.sh` (run from inside `source_code/`, where all of it lives now) — do not hand-edit them.

### System dependencies

pengine (`libpengine.a`, see above), vulkan (+ validation layers), libdrm,
wayland-server, libinput, libudev, xkbcommon, lodepng (`liblodepng.a`), and the
header-only cglm + cgltf. The link line also carries wayland-client,
wayland-egl, EGL, GL and pway, none of which sword itself calls any more: they
are there for symbols in `libpengine.a` that no live path here reaches. libseat
is deliberately **not** linked — see the `-lseat` note in `source_code/Makefile`.

**Header dependencies are tracked now**, in all three repos. sword, pengine and
pway all build with `-MMD -MP` and `-include` the resulting `.d` files, so
touching a header recompiles what reads it. That was not always true, and the
bug it used to cause is worth knowing because its signature is so strange: a
stale object writes its fields at the offsets it was built with, so a `pway.h`
change once left `pway_window_resized()` storing the window width and height on
top of the `pway->key` function pointer, and the next keypress jumped to
`0x3af00000434` — which is just 943 x 1076, the tiled window size. If you ever
see a jump to an address that looks like a pair of screen coordinates, this is
what it is. Adding a member to `PWay` still goes on the **end** of the struct.

**pway** (`/root/pway`) is no longer part of sword's own code — see **There is
no window** below — but it is still on the link line, because `libpengine.a`'s
`pe_vk_create_surface()` references `pway_display`/`pway_surface` in a branch
that is dead here and still has to resolve. That is the only reason for
`-lpway`, `-I/usr/local/include` and `-L/usr/local/lib`. If linking picks up a
stale archive, rebuild it there with `make && make install`.

There is **no X11**, and no XWayland: an X11 client cannot run under sword at
all. Sword drives DRM/KMS directly.

## Architecture

### One thread (`compositor.c`)

**Sword is single-threaded**, the way sway and the other wlroots compositors
are. It used to be three threads — a render loop on `main()`, a compositor
thread, and an input thread — with three mutexes holding them together. All of
that is gone. Any thread left in a `ps` listing belongs to Mesa (the
`sword:disk$0` shader-cache threads, named after the binary), not to sword.

`main()` does setup only — `log_init()`, memory, `init_keyboard()`,
`tty_session_init()`, `pe_vk_init()`, `sword_outputs_init()`, the display
routing capture, `camera_init()`, `sword_init()`, `init_compositor()` — and
then calls `run_compositor()`, which is the whole rest of the program.

**`tty_session_init()` goes before `pe_vk_init()`, and that ordering is the
whole trick.** It takes DRM master, which makes the fd radv opens for itself
non-master, which leaves mesa's `wsi_display` with no fd of its own — so
`pe_vk_acquire_display` can hand it ours instead, and sword can drop the
display again when the VT is switched away. Taking master *after* Vulkan is up
is too late.

**`init_compositor()` and `run_compositor()` are separate.** The first creates
the `wl_display`, registers every global (wl_compositor, xdg_wm_base,
subcompositor, shm, linux-dmabuf, seat/input, output, data device, primary
selection), claims the socket and sets `WAYLAND_DISPLAY`. The socket goes up
only after `pe_vk_init()`, because everything a client touches (the quad's
pipeline, its buffers, the dmabuf format table the GPU is asked for) needs a
Vulkan device; a client that connected before that died on `vkCreateBuffer:
Invalid device` and took sword with it.

`run_compositor()` is then the loop, on **one `poll()`** over exactly three fds:

- `wl_event_loop_get_fd()` — client requests, dispatched with
  `wl_event_loop_dispatch(loop, 0)` (zero timeout: the poll above is what waited)
- a **frame timerfd** at `FRAME_INTERVAL_NS` (~16.7ms), which replaced the render
  loop's old `usleep(16667)`. It must be `read()` when it fires or a periodic
  timerfd stays readable forever after its first expiry
- `libinput_get_fd()`

`wl_display_run()` is this loop with the waiting and the dispatching welded
together inside libwayland; it is unrolled here so input and the frame timer can
be waited on in the same `poll()`.

**Ordering inside the loop matters.** Input is dispatched first, then
`tty_session_handle_pending()`, then the frame step. The VT switch goes after
input so that the keypress asking for the switch is delivered before it
happens, and before the frame step, which must not run once the display is
gone. The frame step itself is guarded by `tty_session_is_active()`: when
another VT owns the screen sword has dropped DRM master, so presenting would be
submitting to a display that is not ours. Clients simply stop getting frame
callbacks, which is what stops them drawing.

`init_keyboard()` runs in `main()`, before any of this. The xkb keymap is the
*compositor's* — every client that calls `wl_seat.get_keyboard` is sent it — so
it cannot live behind the libinput setup, or serving a client dereferences a
NULL `xkb_keymap`. It used to sit behind a branch that one input path never
reached, which is exactly how that happened.

#### There are no locks, and adding one is a mistake

`lock_wayland()`/`unlock_wayland()`, `draw_tasks_mutex`, `focus_task_mutex` and
`retire_mutex` are **all deleted**. They existed for exactly one reason:
**libwayland-server has no locking of its own**, and three threads used to send
events to clients. Everything a client is sent goes through one ring buffer per
connection whose head a send advances and whose tail a flush moves, so two
threads at once lost an update and left the tail *past* the head. The client then
read a message length out of the middle of an event and dropped the connection —
from the outside indistinguishable from the compositor closing it:

```
Data too big for buffer (18446744073709117440 + 8 > 4096).   # -434176, from libwayland's ring buffer
Message length 19200 exceeds limit 4096                       # printed by the *client*
error in client communication (pid ...)
```

One thread makes all of that impossible by construction. Request handlers, the
render step and input dispatch cannot interleave, so a send is never concurrent
with another send. **Do not reintroduce a thread here** without bringing the
whole locking discipline back with it — and note that the old rule was subtle:
`lock_wayland()` was recursive and strictly outermost, before `draw_tasks_mutex`
and `focus_task_mutex`, never after.

What single-threading does **not** remove is GPU-side synchronisation. An shm
upload still waits on the render targets' fences in `begin_frame()`, and
`task_release_old_buffer()` still counts frames in flight — the GPU runs
asynchronously whatever the CPU thread count is. See **Frame path**.

### There is no window

**Sword drives DRM/KMS and nothing else.** `main()` sets `is_drm_rendering =
true` unconditionally; there is no `create_wayland_window()`, no fallback, and
no branch to pick between them. The `wayland_window/` directory that held the
pway nested-window development path is gone, along with the stale `window.o`
that outlived it. `is_wayland_window` is gone from sword entirely, and so is
`is_opengl`, the `extern bool` that used to select the EGL/GLES path (`egl.c`,
`buffers.c`) that no longer exists — it had no definition and no readers by
the end.

That path existed so the compositor could be developed inside another
compositor without a VT switch. It was removed once sword became the thing
being used rather than the thing being tested, and reintroducing it is a real
cost: every `is_wayland_window` branch it needs back is one in pengine as well
as here.

**Resizing is not implemented**, and the reason is not a hardcoded size — every
size here comes from the display mode. `sword_outputs_init()` reads
`pe_render_targets`, and `pe_vk_init()` overwrites `pe_window_width/height`
from the same place, so the layout, the pointer and the camera are all on the
real mode already. What is missing is the teardown: `pe_vk_init()` builds each
target's surface, swap chain, image views, render pass and pipelines exactly
once and nothing rebuilds them, so a mode change has nowhere to go and there is
no monitor hotplug either. That is a pengine-side gap.

### Frame path

`pe_vk_draw_frame()` (`renderer/draw.c`) → `pe_vk_start_render_pass()` → `pe_vk_draw_commands()` → **`sword_draw_scene()` (`sword.c`)**. That last function draws each Wayland client as a textured quad (`draw_surfaces()`) and the cursor on top. **To add or change something visible, edit `sword.c`**, not the renderer.

`sword_init()` (also `sword.c`) is the counterpart: `pe_2d_init()` and `cursor_init()`. `clean_sword()` must free whatever it allocates.

`pe_vk_draw_frame()` **no longer ends in `vkQueueWaitIdle()`** — it keeps
`PE_VK_FRAMES_IN_FLIGHT` frames going on per-frame and per-image fences instead
(pengine's `renderer/draw.c`). Nothing may assume the queue is drained at the end
of a frame any more: `begin_frame()` waits on the targets' fences explicitly, and
only on the frames where an shm client actually redrew.

**The frame has two halves, and which half a thing goes in is not arbitrary.**
`sword_frame_step()` runs `handle_focus()` → `pointer_refresh_focus()` →
`begin_frame()` → `pe_frame_draw()` → `update_delta_time()` → `end_frame()`.

**`begin_frame()` is where shm pixels are uploaded**, and it is on this side of
the draw deliberately. It used to be in `end_frame()`, after `pe_frame_draw()`
had already recorded a quad reading the image — so what an shm client committed
was never on screen until the frame *after*, and every redraw showed the
previous one first. dmabuf never paid that, because its quad samples the
client's pages directly; shm is a copy, and the copy has to happen before the
thing that reads it. It walks `tasks_for_draw` once to see whether any shm
client actually redrew and returns immediately if none did, so the fence wait
below is paid only on the frames that need it.

The fence wait is the price of moving it here. An shm upload rewrites an image
a frame in flight may still be sampling, and the single-time commands it is
made of carry no barrier against that. It waits on **every** render target's
fences, not just one: with multimonitor the fences live on `PRenderTarget`, and
an upload has no way to know which target last drew the surface it is about to
overwrite.

**`end_frame()` is everything that needs the GPU to be past a frame**:
`retire_collect()`, then the frame callbacks, then `task_release_old_buffer()`
per task, then `wl_display_flush_clients()`.

Frame callbacks go to **every surface in `compositor.surfaces`, not just the
ones that were drawn**. A client that asks for a frame callback before it has
ever attached a buffer is waiting on that callback in order to draw its first
one, and a parent surface with all its content in a subsurface may never attach
one at all. Answering only what was drawn left both of them waiting forever.

Anything that submits to `vk_queue` on behalf of a client belongs in one of
these two functions and nowhere else — they are the only points in the loop
where nothing is recording.

### Advertised global versions

`COMPOSITOR_VERSION` and `SEAT_VERSION` (`compositor.h`) are what
`wl_global_create()` advertises for `wl_compositor` and `wl_seat`. **A client
binding above the advertised version is a protocol error and gets
disconnected** — libwayland prints `invalid version for global ... expected at
most N, got M` followed by `error in client communication`, and the client dies
on the registry roundtrip before it ever creates a surface. pway binds both at
4, so both are 4.

Raising a version is not free: every request the new version adds needs a
handler in the interface struct, because **libwayland dispatches a NULL handler
as a call** and takes the compositor down with it. Version 4 of `wl_compositor`
is entirely `wl_surface` requests — `set_buffer_transform` (v2),
`set_buffer_scale` (v3), `damage_buffer` (v4) — all no-ops in
`surface.c` since the quad samples the whole buffer regardless. Child
resources are created with `wl_resource_get_version(resource)` rather than a
hardcoded 1, so a surface or keyboard inherits the version its parent was bound
at. It is sometimes free — `xdg_wm_base` went from 1 to 2 for the tiled
toplevel states and version 2 adds no request at all — but that has to be read
off the generated header's `..._SINCE_VERSION` lines rather than assumed.

The other half of the same rule runs the other way: an **event, or a value in
one, above the version the client actually bound at** has to be gated on
`wl_resource_get_version()`, since a client that bound lower has no way to know
what it means. `top_level_is_tiled()` (`top_level.c`) is the shape of it — a
client on `xdg_wm_base` 1 still gets the empty state array.

**A missing global is the most expensive bug in this file, and it has cost three
clients so far.** It produces no protocol error: the client gets a NULL proxy
back and either segfaults inside libwayland-client or waits forever. pway used
to call `wl_data_device_manager_get_data_device()` on a NULL proxy and die. The
same hole on the primary-selection side only showed up once the seat had a
pointer, because pterminal finishing a mouse selection calls
`pway_primary_copy()`, which marshalled on the NULL manager and died on the
button release — so a click in a client looked like sword closing it (both
guarded in pway now). Then firefox: it bound `wl_compositor`, `wl_output` and
`wl_shm`, created an shm pool, and stopped, printing `gdk_seat_get_keyboard:
assertion 'GDK_IS_SEAT (seat)' failed`. It never bound `wl_seat` and never
reached `xdg_wm_base` at all, because **GDK does not build a seat until
`wl_data_device_manager` exists** — a seat without a clipboard is not something
it will construct — so it postponed the seat forever. (That last diagnosis is
read off the log rather than confirmed by a run; if firefox still stalls, check
`WAYLAND_DEBUG=1` for whether a `wl_registry.bind` of `wl_seat` ever goes out.)

**A client that dies or stalls the moment it uses a new path is worth suspecting
of a missing global before a protocol error** — the protocol error at least
prints. The absence shows up as a *gap in sword's own log*: every bind
handler prints, so the missing line is the diagnosis.

Still not advertised, and each of these is a real capability gap rather than a
detail: **XWayland** (no X11 client runs at all), `wlr_layer_shell`
(which is why the launcher is placed by app id — see **The launcher**),
`xdg_decoration`, `viewporter`, `wp_presentation`, `idle_inhibit`, screencopy
and any screenshot protocol, and `ext_data_control_manager_v1`.

### xdg-shell

`xdg_wm_base` is advertised at version 2 (`compositor.c`), and every
request of every xdg interface has a handler for the reason above — a NULL entry
is dispatched as a call. Version 2 is the one the **tiled toplevel states**
arrived in and it adds no request to any of the four xdg interfaces, so the
bump cost nothing. Version 3 is not free: it adds `xdg_popup.reposition` and
three `xdg_positioner` requests, and going there without writing them takes the
compositor down the first time a client repositions a menu.

`xdg_toplevel` (`top_level.c`) records the
title, app id and size limits and no-ops the rest; `set_maximized`,
`unset_maximized`, `set_fullscreen` and `unset_fullscreen` **must** still answer
with a configure even though sword declines them, because a client blocks
waiting for that configure before it will draw again. `reconfigure()` sends the
size the client already has and neither `maximized` nor `fullscreen` among its
states, which is the protocol's
way of saying no — and since the layout is what keeps `TopLevel.width/height`
up to date, that is automatically the window's own tile rather than a stale
guess. A tiled window is already the only size it is going to get, so declining
is the honest answer to all four.

**A configure with an empty state array is a suggestion, not an order.** The
protocol says the size in it is a *hint* and the client may take its own
instead, and firefox does exactly that: it maps at the cell it was given, then
restores its session and redraws at the size it remembered from the last run —
1054x1883, from a previous session on the rotated monitor. The layout only
sends a configure when the cell *changes*, so nothing ever took that back, and
the window sat squashed into a cell it was nearly twice the height of until
super+f floated it and forced a fresh configure out of
`layout_toggle_floating()`. That "float it and it fixes itself" is the
signature of this bug and not of a configure that never went out — the log
shows the first one going out and being acked.

So `send_top_level_configure()` sends the four `tiled_*` states for a tiled
window. They are what say the size is not the client's to pick. They also tell
a client with its own decorations that every edge of it is against something,
so it narrows the shadow it draws *outside* its window geometry — firefox's
padding went from 26px a side to 20px, buffer 1132x1972 in a 1080x1920 cell.
It does **not** go away: the shadow is the client's own pixels and the
protocol's answer to it is `set_window_geometry`, not a state — see **The
window geometry is what the cell sizes** below. A **floating** window
deliberately gets none of the states: that one really is free-standing, and
the shadow under it is what makes it read as sitting over the tiling rather
than in it.

### The window geometry is what the cell sizes

A configure carries a **window geometry** size, not a buffer size. A client
with its own decorations draws a shadow outside that geometry and hands over a
buffer bigger than the window in it: firefox pads 20px on all four sides, so a
1080x1920 cell comes back as a 1120x1960 buffer. `set_window_geometry`
(`desktop.c`) is where the client says which part of the buffer is the window,
and `task_window_geometry()` (`subcompositor.c`) is what puts *that* rect on
the cell.

Ignoring it is what made firefox blurry, and the symptom is worth recognising
because it is not what a scaled window usually looks like: flat areas are
fine and only the **text** is soft. Scaling a 1120x1960 buffer into a 1080x1920
cell is a 1.037x minification — every client pixel lands between two of the
output's, which solid colour survives and glyph edges do not. A blur that only
the text shows is a small non-integer scale, and the fix is always to find out
why the two sizes differ rather than to filter differently.

`task_origin_and_scale()` still returns the origin of the **buffer** and not of
the window — the buffer is what the quad is drawn from, and a subsurface's own
offset is measured in it. The geometry only moves that origin back by the
shadow, so the window inside it lands on the cell. **That is what keeps the
rest of the file honest**: because the origin is still the buffer's,
`pointer_inside()` dividing `(cursor - origin)` by the scale goes on producing
surface coordinates with the shadow offset already in them, `task_screen_rect()`
goes on reporting the whole buffer — which is what a client's own input region
is measured against — and a subsurface's `child_x * scale` still lands where the
client put it. Returning the *window's* origin instead would have needed the
same 20px added back in three places, and the one that was forgotten would put
firefox's cursor 20px off everything.

What that leaves is **spill**: the shadow is now drawn outside the cell, over
whatever is next to it, since nothing scissors the quad to its tile. It is
transparent, and `texture.frag` discards below alpha 0.1, so it does not read
as a band — but a client whose padding is opaque would show one. The cure is a
`vkCmdSetScissor` in `draw_surface()` (the pipeline already carries
`VK_DYNAMIC_STATE_SCISSOR`), which has to restore `target->scissor` afterwards
since `pe_vk_start_render_pass()` sets it once per pass, and has to apply
`sword_draw_rotated()`'s coordinate map on a rotated output.

A geometry rect that does not fit inside the buffer the client has attached is
refused in favour of the whole buffer. The client may legally send one before
it has attached anything at all.

The **initial** configure comes out of `layout_apply()` too, not from
`get_top_level_implementation()` itself. It is the only configure a client gets
before it is allowed to draw, so the layout has to have run by the time that
function returns.

Configure serials come from `wl_display_next_serial()` and are stored in
`DesktopSurface.pending_serial`, so `do_desktop_ack()` can tell a real ack from
a stale one. A constant serial made every configure look like the same event.

### Popups are real menus (`popup.c`)

`xdg_positioner` and `xdg_popup` used to be a stub that sent `popup_done` the
instant a menu was created — enough to keep a client alive, not enough to have
a menu. **They are implemented now**, and a popup is an ordinary `Task` drawn
as its own quad, so thunar's context menus and gimp's menu bar work.

`positioner_place()` resolves the anchor rect, the anchor and gravity edges and
the client's offset into a position. Two details in it are easy to get wrong.
The anchor rectangle is in the parent's **window geometry**, so it has to have
`geometry_x`/`geometry_y` added to it — a client that leaves a shadow margin
inside its surface (firefox, 20px a side) would otherwise open every menu
offset by that margin. And a menu that does not fit is **slid back inside the
parent** rather than flipped to the other side of its anchor, because nothing
scissors a quad here and a popup hanging off the cell would be drawn over the
window next door. Flipping is what the constraint adjustment usually asks for
and would keep a submenu beside its parent item instead of on top of it; the
`//TODO` says so.

**A grab is two obligations, and leaving out either one makes the menu close
the moment the button comes up.** `popup_grab()` records the grab, and then:
every press outside the popup must dismiss it (`pointer.c` calls
`popups_dismiss_outside()`), and the **keyboard must go to the popup** for as
long as it is up. GTK asks for the grab and then waits to be told it has the
focus; never being told, it decides the grab is broken and tears the menu down
itself. `popup_grab_task()` is what `handle_focus()` asks every frame, and it
walks the list newest-first so a submenu holds the keyboard over the menu it
came out of.

Popups are a `wl_list` oldest-first, chained parent to child, so a menu with a
submenu open is two of them. `popups_dismiss_outside()` walks it in reverse and
skips anything the pressed surface is *inside* — `task_is_inside()` follows
`task->parent` up, so a subsurface of the menu or a submenu hanging off it does
not dismiss its own parent. It sends `popup_done` and stops there: the client
answers by destroying the `xdg_popup`, and *that* is what takes it off the
list. Removing it here as well would drop it twice.

**Every `xdg_popup` and `xdg_positioner` request has a handler, including all
three that version 3 added** (`set_reactive`, `set_parent_size`,
`set_parent_configure`) plus `xdg_popup.reposition`. So the version 3 bump on
`xdg_wm_base` would no longer take the compositor down — but `reposition` is an
empty stub that sends no `repositioned` event and no configure, so a client
that repositioned a menu would be left waiting. Write it before bumping, not
after.

### Subsurfaces: a window is a tree (`subcompositor.c`)

`wl_subcompositor` is advertised, and it is **not optional for a GTK client** —
firefox aborts outright without it rather than degrading. A window is therefore
a tree of surfaces, not one surface: `Task.parent`, `Task.child_x/child_y`, and
`draw_surfaces()` walking the tree rather than a flat list.

`task_origin_and_scale()` is the heart of it and is worth reading before
touching anything here. It answers where a surface's origin lands on the
virtual desktop and how much its coordinates are stretched getting there, and
it is **deliberately separate from the surface's size**. A surface with no
buffer still has a position and things hang off it — firefox's menus are
exactly that shape: the popup's own `wl_surface` carries no pixels and the menu
is drawn by a subsurface inside it. Requiring a buffer to work out a rectangle
made that whole subtree unpositionable and the menu never appeared at all.

A child inherits its parent's scale and is placed in the parent's *surface*
coordinates, so its offset is stretched the same way the parent's pixels are,
and it carries that scale down to its own children. That is the same ratio
`pointer_inside()` divides the cursor back through — which is why the branch
lives here and not in `draw_surface()`.

`task_screen_rect()` builds on it: a surface with no buffer gets a position and
a **zero extent**, and both callers read that correctly — nothing is drawn for
it, and the cursor is inside nothing.

`log_surface_tree()` dumps every surface, its role, what it hangs off and where
that puts it. A window made of a tree of surfaces has no other way of being
read, and `end_frame()` can fire it a few frames after a popup maps
(`surface_tree_dump_countdown`) which is exactly when a menu that went to the
wrong place is worth looking at.

`task_detach_subsurfaces()` and `forget_subsurface_role()` are the teardown
halves, same shape as everything else here: whichever object dies first has to
let go of the other.

**Synchronized mode is not implemented** — a `//TODO` in the file says so. The
protocol has a synchronized child's commits cached and applied with its
parent's; sword applies them immediately.

### Input regions (`region.c`)

`wl_compositor.create_region` cannot be left NULL — libwayland dispatches a NULL
handler as a call — and the region it makes is what a client hands to
`wl_surface.set_input_region` or `set_opaque_region`.

A `Region` is stored as the **operations in the order the client sent them**,
not as a resolved set of rectangles. That costs nothing and answers the only
question sword asks exactly: `region_contains()` replays the adds and subtracts
in order, so a point inside an add is in and a point inside a later subtract is
out. No region algebra is needed to get an arbitrary add/subtract list right.

The input region is honoured in the pointer hit test, which is what stops a
client's shadow — part of its buffer, and not part of its window — from
swallowing clicks meant for whatever is behind it.

### The tiling layout (`layout.c`)

The window manager half, and deliberately **policy only**: it writes a rectangle
onto every `Task` and `draw_surface()` puts the quad where it says. Nothing in
it touches the GPU, which is what lets it run straight from the request handlers where the
requests arrive.

The layout is a **spiral**, applied per output — `layout_apply()` walks every
`SwordOutput` and tiles each one separately, so a window belongs to the monitor
its `output_index` names. Each window takes half of what is left and the split
direction cycles right, down, left, up, so the free area winds inward — the
oldest window keeps the biggest cell and a new one always appears beside the one
before it. `LAYOUT_SPIRAL 0` switches it to dwindle, where the free area only
ever walks toward the bottom right. The arithmetic is worth checking against a
pixel-counting harness when it changes: at one through seven windows the cells
must cover the output exactly, with no overlap and no hole. `LAYOUT_GAP` is the
space *between* two windows; every cell gives up half of it on each side and the
output starts out short of the same half, so the margin at the screen edge comes
out the same width as the gap between neighbours. It is **0** right now.

**A window can be floating instead of tiled.** `layout_toggle_floating()`
(super+space) flips `Task.is_floating`; a floating window is skipped by the
tiling entirely, keeps its own rectangle, stacks above the tiled ones, and is
raised among the other floats by `layout_raise()`. It is also the one kind of
window that can be dragged, including **across monitors** — `apply_drag()` in
`pointer.c` moves it through the virtual coordinate space and its
`output_index` follows. A floating window deliberately gets none of the
`tiled_*` states in its configure: it really is free-standing, and the shadow a
client draws under it is what makes it read as sitting over the tiling rather
than in it.

Floating is also the escape hatch for the configure-is-a-suggestion problem
below — flipping a window floating forces a fresh configure out of
`layout_toggle_floating()`, which is why "float it and it fixes itself" is the
signature of a client that ignored its cell.

**There is no window list of its own.** `compositor.surfaces` already holds every
`Task` with lifetimes that are known to be right, so the layout walks that and
skips whatever is not a window. A second list would be another thing to keep in
step with client teardown, and teardown is where this file's bugs live. Surfaces
are inserted at the **head** in `create_surface()`, so the walk is
`wl_list_for_each_reverse` to get map order.

A window is only sent a configure when its size actually **changed** — a
relayout that reaches every client on every map would have all of them repaint
for nothing.

`Task.top_level` is what says a surface is a window rather than a cursor image
or a bare `wl_surface`, and it is what a close is sent on. **That pointer needs
a destroy listener**, exactly like the `wl_buffer` ones beside it. Having the
toplevel's destructor clear it the other way round — through
`top_level->surface->surface` — killed the compositor every time a window
closed: libwayland tears a disconnecting client's resources down in the order
they were **created**, so the `wl_surface` and its `Task` are already freed by
the time the `xdg_toplevel` destructor runs. `handle_top_level_destroyed()` and
`task_stop_listening_to_top_level()` are the two halves, so whichever object
dies first lets go of the other.

`Task.tile_*` is written here and read by `draw_surface()` a moment later in the
same loop, so it needs no synchronisation. Focus cycling only stores a different
`Task` in `focused_task`; the frame step's `handle_focus()` turns that into
`wl_keyboard.leave`, `enter` and a fresh clipboard offer. `layout_close_focused()`
still calls `wl_display_flush_clients()` by hand so the close reaches the client
now rather than at the top of the loop's next iteration.

**Nothing hands the focus on by itself when a window dies.** `forget_task()`
(`input.c`) has to NULL `focused_task` — the `Task` is about to be freed — and
that used to be the end of it: after super+c the keyboard belonged to nobody and
the next key went nowhere. `layout_focus_fallback()` is the other half, called
from both teardown paths, since either object may die first: `destroy_surface()`
after its `layout_apply()`, and `destroy_top_level_resource()` after its, for the
client that destroys the `xdg_toplevel` and keeps the `wl_surface` — that `Task`
is still alive but is no longer a window, so it cannot hold the keyboard. It is a
no-op while `focused_task` is still a live window, which is what makes it safe on
a path where the window that closed was not the focused one. It walks
`compositor.surfaces` **forwards**, unlike the layout: surfaces go in at the
head, so that is newest first and the focus lands on the window the closed one
was mapped over rather than on the oldest one on screen.

Shortcuts live in `handle_sword_key()` (`keyboard.c`), keyed on the unicode the
keymap produced, and everything is behind **super** so a client can still type
the letters:

| | |
|---|---|
| super+enter | pterminal |
| super+m | firefox |
| super+d | `pmenu_run`, the launcher |
| super+h / super+l | cycle the focus |
| super+c, super+shift+q | close the focused window |
| super+space | toggle the focused window floating |
| super+shift+c | close the compositor |
| super+w | switch to `SWITCH_TO_VT_NUMBER` |

`xdg_toplevel.close` is a *request*; a client with unsaved work may put up a
dialog and stay.

Unconditional shortcuts are what this replaced, and the failure was obvious in
use: a terminal could never type `d`, and typing `q` killed the compositor.

### The launcher

**The launcher is not part of sword.** `super+d` runs `/usr/bin/pmenu_run`, a
script outside this repo: `pmenu` only filters a list and prints what was
picked, and `pmenu_run` is what feeds it `$PATH` and runs the answer.

Because there is no `wlr_layer_shell`, pmenu cannot ask to be a panel. Sword
recognises it instead: `layout_place_launcher()` matches `app_id` against
`LAUNCHER_APP_ID` ("pmenu") and gives that client a `LAUNCHER_HEIGHT` (40px)
strip across the top of its output, floating and raised, rather than a cell in
the tiling. Every other app id falls straight through. It is a deliberate
stopgap — the honest fix is layer-shell — and it is why the launcher's app id
is load-bearing: pway sets one now precisely so this can key off it.

**ctrl+alt+Fn / ctrl+alt+number** switch VT, and are the one shortcut not behind
super: it is the gesture the kernel console answers, and `tty_silence_keyboard()`
putting the console in `K_OFF` is exactly what stops it answering — so the
compositor has to do the switch itself. Both
spellings are taken (F1 and 1 are both tty1, 0 is tty10), plus the
`XF86Switch_VT_n` keysyms a keymap may translate ctrl+alt+Fn into by itself.
The switch is where `input_release_pressed_keys()` (`input.c`) earns its place:
libinput is suspended before the release of ctrl, alt and the digit ever
arrives, so without it the client repeats the last key forever and sword's
own `xkb_state` goes on reporting ctrl+alt — the next digit typed would switch
VT again. `session_deactivate()` calls it while the keyboard is still ours.

An **unmap** (`surface_attach()` with a NULL buffer) keeps its cell rather than
reflowing. Reflowing there costs the window its place to whoever is behind it
and hands it a different one when it maps again, which is worse until the layout
keeps a stable order of its own — the `//TODO` says so rather than leaving it
looking like an oversight.

`move`, `resize` and `show_window_menu` stay no-ops: a tiled window's size is
not the client's to ask for.

### The monitors (`outputs.c`) and the outputs (`output.c`)

These are two different files and two different jobs. **`outputs.c` owns the
physical monitors**; `output.c` is the `wl_output` protocol on top of them.

`SwordOutput` (`outputs.h`) is one physical monitor, and they are laid left to
right in **a single virtual coordinate space** that `layout.c` tiles into and
`mouse.c`'s cursor moves through. Two things about that table are traps. Its
own order is the render-target order and never changes, but the x origins are
handed out **rotated outputs first**, so a portrait monitor sits at the left of
the virtual desktop — index order is therefore *not* left-to-right order, and
nothing may take `sword_outputs[0]` for the leftmost or the last entry for the
rightmost. Use `sword_output_at()` / `sword_output_index_at()`.

**Rotation is entirely in software.** `SWORD_OUTPUT_ROTATE` is a comma-separated
list of output indices to mount 90 degrees counter-clockwise. There is no
hardware rotation to ask for: mesa's `wsi_display` hardcodes
`VkDisplayPropertiesKHR.supportedTransforms` to IDENTITY only, so sword rotates
what it draws, once per quad, in `sword_draw_rotated()`. The `rotated` flag is
deliberately invisible to everything else — `layout.c`, `mouse.c`,
`draw_surface()` and `cursor.c` all work in the output's **logical**
(already-rotated) width and height and never read it. `sword_draw_rotated()` is
the single place that looks past it at the physical target underneath. Keep it
that way: a second reader of that flag is a second chance to rotate twice.

**DRM routing has to be captured and restored.** `sword_capture_display_routing()`
records the connector→CRTC mapping at startup and `sword_restore_display_routing()`
puts it back, because a VT switch away and back hands the connectors over in a
different order and the windows end up on the wrong monitors.
`sword_sort_displays_by_connector()` is the `pe_vk_sort_displays` hook that keeps
the render targets in connector order to begin with.

**`output.c` creates one `wl_output` global per monitor**, at version 4, each
describing its own `SwordOutput`: its logical size at 60000 mHz, flagged
`CURRENT | PREFERRED` since there is nothing to switch to. `send_output_state()`
is the single place that describes one. Every event above version 1 is gated on
the version the client bound at, and `release` — the only request the interface
has — has a handler.

Two details the protocol is easy to get wrong on. **`done` is not a formality**:
a client applies the whole burst as one unit, so leaving it out leaves the client
holding an output it never commits. And a physical size of **zero** is what the
protocol says to send for an unknown one, but a client that divides by it to get
a DPI gets an infinity — so the panel is described as an ordinary 96 DPI one and
the arithmetic lands somewhere.

`wl_surface.enter` goes out from `get_top_level_implementation()`, when a surface
becomes a window rather than when it is created — the same reason focus is
claimed there: a bare `wl_surface` may be a cursor image, and a cursor is on no
output. It assumes the client bound `wl_output` in the same registry pass it
bound `wl_compositor` in, which is how every client is written. The resources
live in a `wl_list` (via `wl_resource_get_link()`) because a client may bind the
global more than once, and enter is owed on each of its own.

pway never binds `wl_output`, so pterminal exercises none of this. It exists for
the toolkit clients that would otherwise stall.

### The clipboard (`data_device.c`, `primary_selection.c`)

`wl_data_device_manager` at version 3 — see **Advertised global versions** for
why GDK will not start without it. **None of the data passes through the
compositor.** It keeps whichever `wl_data_source` last claimed the selection,
tells the focused client which mime types are on offer, and when that client
sends `wl_data_offer.receive` it hands the pipe fd straight to the source client
through `wl_data_source.send`, which does the writing. Our own copy of the fd is
closed afterwards: libwayland dups it into the source's connection, and holding
the write end open leaves the reader waiting on an EOF that never comes.

**A `wl_data_offer` belongs to one client**, so sending it once when the
selection is set is not enough. `send_keyboard_enter()` calls
`data_device_offer_selection()` on every focus change, and that is what makes a
copy in one window paste into another. `keyboard_focus_client()` (`input.c`) is
how the clipboard finds out who to offer to; the reads and the writes of
`keyboard_focus` all happen on the one thread, so it needs no synchronisation.

The offer is taken back the same way it was given. `set_keyboard_focus()` sends
the client that is losing the keyboard a `selection` event with a **NULL** offer
— `data_device_clear_selection()` and `primary_selection_clear_selection()`,
which are the offer walk with a NULL in place of the offer. The protocol has a
client destroy its previous offer when that arrives, and only the focused client
is entitled to a non-NULL one; a client that is never told keeps an offer of a
source it can no longer read. The guard is `keyboard_focus_client()`, which is
NULL until a client has actually been sent an `enter`, so a client that never
got an offer is not sent a clearing event it cannot make sense of.

The source belongs to a client that can destroy it while another client is still
holding an offer of it, so **every offer carries a `wl_listener` on the source's
destroy signal** — the same shape as a `wl_buffer` outliving the surface that
attached it. The handler NULLs the pointer and re-inits the link so the offer's
own destructor can remove it again. The previous owner gets
`wl_data_source.cancelled` the moment it stops being the owner; a source that is
never told goes on believing it holds the clipboard.

There is **no drag** — no icon surface, no grab, no `wl_data_device.enter` to
send anyone — so `start_drag` answers with `wl_data_source.cancelled`. That is
the only reply that leaves the client alive and unstuck, exactly as `popup.c`
answers with `popup_done`; silence hangs a client waiting on a drop. The rest of
the version 3 drag-and-drop requests are present and do nothing.

`set_selection` does **not** check the serial. It should be matched against the
input event the client is claiming the clipboard for, and sword does not keep
its serials long enough to tell — refusing on one it cannot verify would mean no
clipboard at all.

### The pointer

The seat advertises `WL_SEAT_CAPABILITY_POINTER` alongside the keyboard.
`mouse.c` owns the cursor position, in the **virtual desktop's** coordinates —
the space every monitor is laid out in, not any one output's — and
`device_input.c` feeds it from libinput. `input.c` turns a cursor position into
`wl_pointer` events.

One thing about libinput is easy to get wrong: it
sends the deprecated `LIBINPUT_EVENT_POINTER_AXIS` **as well as** the typed
`..._SCROLL_WHEEL`/`_FINGER`/`_CONTINUOUS` events, so handling both counts every
scroll twice.

`pointer_hit_task()` is the whole hit test: it walks `compositor.surfaces` and
takes the first window whose **tile** the cursor is inside. Order does not
matter while the cells cannot overlap. It skips anything without a
`top_level` — a cursor image or a surface still on its way to being a window
would otherwise take the pointer over the whole rectangle it would be drawn at.

**Click to focus** is in `send_wayland_pointer_button()`. The pointer and the
keyboard are two separate focuses: `pointer_focus` follows the cursor by itself,
but the keyboard follows `focused_task`, which only a new window and super+h/l
ever moved — so clicking a terminal sent the pointer there and left the keys
going wherever they already went. A **press** stores `pointer_focus` in
`focused_task` and the frame step's `handle_focus()` turns that into
`wl_keyboard.leave`, `enter` and a fresh clipboard offer, exactly as it does for
`layout_focus_next()`. Only the press: a release belongs to whoever took the
press, and moving the focus on it would hand the keyboard away when the button
comes up over a different tile.

The **second scale** lives here. A client's buffer may be stretched into its
tile, so the buffer is not the size of the rectangle on screen and
`pointer_inside()` has to divide the position back into buffer coordinates —
a client told the cursor is at the tile's own coordinates draws its cursor
somewhere other than where the user is pointing, which is the same bug as the
outer scale one level up. Both sides read the scale off the same function,
`task_origin_and_scale()` (`subcompositor.c`), which is why the branch below
belongs there and not in `draw_surface()`.

**A buffer that fits its cell is drawn at its own size, not stretched up to
fill it.** The stretch only ever covered the frames between a configure and
the client repainting, and in the direction where the cell *grew* — close a
window and the survivor is handed the whole output — that cover was a 2x
upscale of the old buffer: four frames (~66ms, measured) of a visibly blurred
window. An unpainted band beside a crisp window reads better, and it is what
every other tiler shows there. The other direction still stretches: minifying
for two frames does not read as broken, and a buffer *larger* than its cell
drawn at its own size would spill over the window next to it, which nothing
here scissors against. Choosing 1:1 only when the buffer already fits inside
the cell is what makes the spill impossible by construction rather than by a
clip.

A client's **cursor image is a surface like any other** — it comes through
`wl_compositor.create_surface` and would otherwise be drawn as a full quad in
the scene and take the focus that goes with a new surface. Two things stop that:
focus is claimed in `get_top_level_implementation()` rather than
`create_surface()`, because a wl_surface is not necessarily a window; and
`wl_pointer.set_cursor` calls `mark_surface_as_cursor()`, which pulls the task
back out of `tasks_for_draw` and releases its buffer by hand, since nothing in
the render loop ever will. There is no cursor drawn anywhere.

`wl_keyboard` and `wl_pointer` are separate resources carrying the same
`TaskInput`, and on a client disconnect libwayland may destroy them **after**
the seat. `destroy_task_input()` NULLs their user data before freeing, and both
destructors check for it.

### DRM formats and the dmabuf import

Client buffers arrive as a DRM fourcc, and `drm_format.c` is the only
place that says what that means in Vulkan. The two naming schemes read backwards
from each other — a fourcc names its channels from the top of a little-endian
32-bit word down, Vulkan names them in memory order — so `XR24` is X,R,G,B in
the word and B,G,R,A in memory, which is `VK_FORMAT_B8G8R8A8`. Several common
fourccs have **no** Vulkan equivalent at all (`BGRA8888` is A,R,G,B in memory);
they must not be advertised, because importing one anyway rotates every channel.

The 8-bit formats map to **`_SRGB`, not `_UNORM`**. A Wayland client renders
sRGB-encoded pixels, the swapchain is `VK_FORMAT_B8G8R8A8_SRGB`, and every
texture the engine loads is `_SRGB` too. Sampling a client buffer as `_UNORM`
hands the framebuffer a value that is already gamma-encoded and lets it encode
the whole thing a second time — client windows came out washed out next to the
rest of the scene. 10- and 16-bit formats are deliberately not in the table:
Vulkan has no `A2R10G10B10_SRGB`, so the round trip cannot be done in hardware,
and merely offering 10-bit moved Mesa onto it (a client that does not ask for a
depth takes the first config that matches).

`pe_vk_import_image()` must use `VkImageDrmFormatModifierExplicitCreateInfoEXT`
and pass the client's own offsets and row pitches. The `...ListCreateInfoEXT`
form lets Vulkan pick a layout, which is right for an image Vulkan allocates and
wrong for one that already exists elsewhere: Mesa rounds an 800-pixel row up to
3584 bytes, not 3200. X formats also leave the fourth byte undefined, so the
image view sets `components.a = VK_COMPONENT_SWIZZLE_ONE`.

The advertised format table in `feedback.c` is built once at first
`get_default_feedback` by asking the GPU, via
`vkGetPhysicalDeviceFormatProperties2` + `VkDrmFormatModifierPropertiesListEXT`,
which modifiers it can sample each format with — it used to be two hardcoded
guesses with modifier 0. On this machine that is 16 pairs, and clients pick an
AMD DCC modifier rather than linear.

### Wayland client → quad

A client surface becomes a `Task` (`compositor.h`): it owns the `wl_resource`, the client's buffer, a `PTexture`, and a `PModel` quad — plus, once the client gives it a toplevel role, the tile the layout put it in and a listener-backed pointer to its `TopLevel`. `Task`s live in the `tasks_for_draw` array; each frame `begin_frame()` uploads shm pixels, `draw_surfaces()` draws the tree, and `end_frame()` sends the frame callbacks (`send_frame_callback_done`), pays owed releases, and then `wl_display_flush_clients`.

#### Both buffer protocols make the same struct

Buffers arrive either through wl_shm (`shared_memory.c`) or
linux-dmabuf (`dma.c`). **Both put a `ClientBuffer`
(`client_buffer.h`) behind the `wl_buffer`'s user data**, tagged with
`type` as its first member, because that user data is all `surface_attach()` has
to go on. They used to put *different* structs there — a bare `PTexture` on the
dmabuf side, its own bookkeeping struct on the shm side — and `surface_attach()`
believed the first unconditionally. A `PTexture` is nearly twice the size, so the
`memcpy` into the quad ran off the end of the allocation and the quad got a
`VkImage` handle made of whatever heap followed it. Nothing had hit it because
pterminal is dmabuf and no shm client had ever got a window up.

`Task.client_buffer` carries it alongside `Task.image`, because nearly everything
about the two kinds differs from that point on.

#### shm is a copy, dmabuf is not, and everything follows from that

A dmabuf is sampled zero-copy out of the client's own memory. An shm buffer is a
mapping of the client's memory, so it needs a real copy onto the GPU — and it
needs one **again every time the client redraws**. A client that draws into a
buffer it has already attached commits without attaching, which is why
`surface_commit()` is not empty: it sets `needs_upload`.

**The copy runs out of `end_frame()`.**
`pe_vk_end_single_time_cmd()` submits to `vk_queue` and waits on it, and a
doing the copy where the commit arrives would submit in the middle of the frame
the loop is recording. `end_frame()` is the one point in the frame where nothing
is recording. The cost is that shm pixels land in the *next* frame:
one frame of latency dmabuf does not pay.

Details in `shared_memory_upload()` that are easy to get wrong:

- The image is created **lazily on the first upload**, not at `create_buffer`,
  because creating it means a layout transition and that is a queue submit.
  `can_draw` stays false until then, or the quad binds a `VK_NULL_HANDLE`.
- Transitioned `UNDEFINED → TRANSFER_DST_OPTIMAL` **once** and left there —
  pengine samples its own textures in that layout too — so every later upload is
  a copy and nothing else.
- The staging buffer is allocated **once per `wl_buffer` and rewritten**, with the
  rows packed straight into the mapped memory. A window this size is 8 MB and a
  `vkAllocateMemory` per frame costs more than the copy.
- `vkCmdCopyBufferToImage` is told nothing about the client's stride, so removing
  the row padding is what the staging copy is *for*.
- The image view is built by hand rather than with `pe_vk_create_image_view()`,
  which has no way to say it: XRGB8888 leaves the fourth byte undefined, so
  without `VK_COMPONENT_SWIZZLE_ONE` the quad blends with whatever the toolkit
  left there. `pe_vk_import_image()` answers it the same way.
- An shm image is destroyed through a **retire list drained in `end_frame()`**,
  because the client can destroy a buffer while a frame still in flight is
  sampling its image.

Three more things `shared_memory.c` used to get wrong, all worth not
reintroducing: `wl_shm.format` was never sent at all (`wl_display_add_shm_format()`
feeds libwayland's *own* shm implementation, the one `wl_display_init_shm()`
creates, which sword does not use — the events go out on bind now, since a
client that binds later would hear nothing); destroying a pool munmapped and
freed it without destroying the resource, so the next request read freed memory,
and the protocol says the mapping outlives the pool until the last buffer cut
from it is gone (reference counted now); and the pool's fd was never closed.

Teardown belongs in `destroy_surface()` — the resource destructor — not in the `wl_surface.destroy` handler, which runs before the `Task` leaves `tasks_for_draw` and so can free a Vulkan image a frame in flight is still sampling. `task->image` is also **NULL until the first attach**, and `pe_vk_clean_image()` reads straight through the pointer: a client that creates a surface and destroys it without ever drawing used to segfault the compositor. That function destroys the **view before the image** — the other order is a validation error, since an image cannot go while a view of it exists.

#### When `wl_buffer.release` is owed

The two protocols answer this **in opposite ways**, and both answers matter.

An shm buffer has been copied, so it goes back **immediately** —
`task_upload_shared_memory()` releases the *current* buffer right after the copy.
The compositor is reading its own image now, and holding it stalls the client for
nothing. The `buffer_released` flag keeps `surface_attach()` from owing a release
on it a second time.

A dmabuf is owed release **when a newer one replaces it**, and not one frame
before. This is the part that is easy to get wrong twice over.
Sending it every frame is obviously wrong. But sending it once, the first frame
the buffer is drawn, is wrong too, and it is the subtler bug: a dmabuf buffer is
sampled zero-copy out of the client's own memory, and `draw_surface()` samples
that same `VkImage` again every frame until the client attaches another one. A
release the first time it is drawn hands back a buffer that is still on screen.
After a frame or two **every** buffer in the client's pool is marked free, mesa
picks the visible one as its next render target, and the frame that lands
mid-repaint shows the clear instead of the content — a flicker whenever the
client redraws, which for a terminal means whenever a key is pressed.

So `surface_attach()` calls `owe_release_on()` for the buffer being replaced and
`end_frame()` pays it through `task_release_old_buffer()`. `end_frame()` is the
earliest safe point: `task_release_old_buffer()` checks the retire frame counter,
so the release waits until the GPU is provably past every frame that sampled it. Releasing inside
`surface_attach()` instead would be too early — attach can land between
`draw_surfaces()` dropping the wayland lock and the queue submit.

A client may destroy a buffer it has been given back, and the `wl_resource` is
then gone while the `Task` still points at it, so `surface_attach()` registers
`handle_buffer_destroyed()` on each buffer to NULL `task->buffer_resource`. The
buffer awaiting release needs its **own** listener (`old_buffer_destroy`),
because `listen_to_buffer()` moves the first one onto the new buffer and the
client can destroy the old one inside that one-frame gap.

**Both protocols go through the retire list now**, and that used to be the
dmabuf side's biggest hole. `dma.c:destroy_buffer_resource()` calls
`retire_texture()`, and it is registered as the *resource* destructor rather
than only as the `destroy` request handler — so it also runs when a client
disconnects without asking, which is what used to leak every imported image a
dying client left behind. `shared_memory.c:destroy_buffer_function()` retires
the texture and the staging buffer both.

The other half is `handle_buffer_destroyed()` (`surface.c`), which drops
`can_draw`, NULLs `task->image` and pulls the task out of `tasks_for_draw`. The
dma side used to keep drawing "what it had", which by then was a `PTexture` the
buffer's destructor had already freed.

Neither may destroy anything where the request arrives: that would destroy an
image a frame in flight is still sampling, which is what the validation layer
reported as destroying a view still in use by a descriptor set. `retire.c`
counts frames and `retire_collect()` in `end_frame()` does the freeing.

Checked against a live session on 2026-09-02: 49 imports against 46 retires
with three windows still open, the live count flat between 2 and 4 across the
whole run, params balanced 49/49, and the 128-slot retire list never overflowed
with 342 buffers through it. If you are hunting a leak, `retire_slot()`
returning NULL logs "Too many retired vulkan objects, leaking one" and is the
one place that admits to dropping something.

### The log (`log.c`)

On tty3 there is no terminal to read. The compositor owns the display, so
everything `printf` writes to the console is either drawn over by the next frame
or never visible at all — and a crash that takes the session with it takes the
output with it. `log_init()` (first line of `main()`, before `pe_init_memory()`)
opens **`/tmp/sword.log`**, or `$SWORD_LOG`, and `log_info(...)` /
`log_debug` / `log_warn` / `log_error` write a timestamped record to it.
The previous run is kept as `<path>.old`, since the run that crashed is the one
worth reading and it is easy to start the next one before reading it.

**Sword's own diagnostics are all `log_*` calls** — the ~150 `printf`,
`fprintf(stderr, ...)` and `perror()` calls that used to be scattered across
these files were converted, and a new one should be too. `log_debug` is where
the per-frame and per-buffer chatter went (`surface.c`'s attach/commit/damage,
`dma.c`'s plane and retire lines, `seat.c`'s dispatch loop), so
`SWORD_LOG_LEVEL=info` leaves a log that can be read.

Three things it does that a `printf` wrapper would not:

- **`log_redirect_stdio()`** dups the log fd onto stdout and stderr, so what
  sword does *not* write itself — pengine's `LOG` (which is still `printf`),
  the Vulkan validation layer, libwayland's own errors — lands in the same file
  anyway. `main()` calls it unconditionally now that DRM is the only path —
  there is no terminal behind the frames to write to. The mirror to stderr the
  records would otherwise get is switched off at the same time, or the file
  would hold everything twice. stdout is put back to **line buffered**
  afterwards: a log
  file is a regular file, so it would otherwise be fully buffered and the last
  4 KB before a crash — the interesting ones — would never be written.
- **A crash handler** on SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL writes the signal
  number and a `backtrace_symbols_fd()` stack to the log, then restores
  `SIG_DFL` and re-raises so the core dump and exit status are unchanged. It
  runs on a dying process, so it is `write()` and nothing else — no `printf`, no
  `malloc`. `backtrace()` allocates on its first call, which is why `log_init()`
  makes that call while the program is still healthy. **`-rdynamic` in
  `source_code/Makefile` is what makes the trace readable**; without it the
  stack is a column of addresses with no names.
- **`SWORD_LOG_SYNC=1`** `fdatasync`es every record. The page cache keeps
  what a process wrote before it segfaulted, but not what it wrote before it
  hard-locked the machine — which is the failure a KMS compositor actually has.
  `SWORD_LOG_LEVEL=debug|info|warn|error` filters.

### Layer conventions

- **`renderer/`** (in pengine) — thin Vulkan wrapper, everything prefixed `pe_vk_`. `pe_vk_init()` in `renderer/vulkan.c` is the authoritative, order-sensitive init sequence; `pe_vk_end()` is its mirror.
- **`engine/`** (in pengine) — reusable engine (hence the `pe_` prefix): custom allocator, `Array` container, glTF/PNG loading, camera, 2D/text.
- **The Wayland server implementation** — `compositor.c`, `surface.c`, `tasks.c`, `top_level.c`, `desktop.c`, `popup.c`, `subcompositor.c`, `region.c`, `data_device.c`, `primary_selection.c`, `output.c`, `dma.c`, `feedback.c`, `drm_format.c`, `shared_memory.c`, `retire.c`. All of it is flat in `source_code/`, and it *is* the main code, not a piece tucked away from it. It includes the one piece of window-manager *policy* (`layout.c`): which window gets which piece of the output. It is written against `Task` and `TopLevel` and sends configures, not because it draws anything.
- Everything else at the top level — `main.c`, `device_input.c` (the libinput event pump), `keyboard.c`, `mouse.c`, `pointer.c`, `input.c`, `outputs.c` (the physical monitors), `launch.c` (spawning a program from a keybinding), `sword.c` (compositor-side drawing: client quads, the cursor), `cursor.c`, `tty.c` (VT and DRM master), `log.c` (see **The log**).

### Memory

`engine/memory.c` allocates one block up front (`pe_init_memory()` at startup, `clear_engine_memory()` at exit) and hands out bump/stack allocations from it. **It is 16 MB**, down from 750 MB: measured usage under normal load is ~15 KB, because only `Array`s and bookkeeping structs come from the arena — client buffers and textures go through Vulkan and never touch it. The old size cost that much RSS on every startup, since `pe_init_memory()` memsets what it takes. `Array` (`engine/array.h`) is the generic growable container built on top and is used everywhere instead of raw malloc'd buffers. Prefer these over `malloc` for engine data.

### Pipelines and descriptor sets

Two descriptor set layouts exist, created in `pe_vk_init()`:

- `pe_vk_descriptor_set_layout` + `pe_vk_pipeline_layout_with_descriptors` — UBO only.
- `pe_vk_descriptor_set_layout_with_texture` + `pe_vk_pipeline_layout3` — UBO + combined image sampler (the city, and all Wayland surface quads).

The UBO is bound **vertex-stage only** in both layouts, so a fragment shader cannot read it. Anything the fragment stage needs must arrive as a varying — `city.vert` forwards the animation clock that way, smuggled through the otherwise unused `light_position.w`.

### Instanced drawing

`pe_vk_create_shader_instanced()` builds a pipeline whose vertex input has two bindings: binding 0 steps a mesh per vertex, binding 1 steps a `PInstance` array per copy. `pe_vk_draw_model_instanced()` binds both and issues one `vkCmdDrawIndexed` for the lot. Locations 0–5 belong to `PVertex`, so instance attributes start at 6 (see `pe_vk_vertex_get_instanced_input()`).

A model's `PCreateShaderInfo.layout` must match the layout its descriptor sets were created with, and the same layout must be passed in `PDrawModelCommand.layout` at draw time. Mismatches here are the usual cause of validation errors.

## Code style

Follow the surrounding code: 2-space indent, `snake_case`, designated initializers for Vulkan structs, `log_info(...)`/`log_debug`/`log_warn`/`log_error` for diagnostics (never `printf` — see **The log**), `VKVALID(call, "message")` for Vulkan results. Headers use `#ifndef NAME_H` guards. Comments are sparse; `//INFO` and `//TODO` mark notes the author cares about — leave them in place.
