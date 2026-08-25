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
`launch_program()` survived, in `launch.c`, because a keybinding still spawns a
terminal.

**The movie premise is gone, deliberately.** Compositing client windows *into* a
3D world only worked while one binary owned both. Do not reintroduce the scene
here — 3dtop is a client like any other now, and is sword's first-party test
client for the dmabuf import path, the multimonitor layout, and rotation.

## Build / install

The repo path is hardcoded in the build (`-I/root/sword/source_code` in `source_code/Makefile`, and in `generate_compile_commands.sh`), so it must live at `/root/sword`.

```sh
make                  # root Makefile: builds AND runs `make install` (needs root)
make -C source_code   # build only -> ./sword
make clean            # removes binary, *.o, ../shaders/*.spv
./source_code/generate_compile_commands.sh   # regenerate compile_commands.json for clangd
```

`make install` copies the binary to `/usr/bin` and `shaders/`, `models/`, `images/*` to `/usr/libexec/sword/`. **Assets are loaded at runtime from absolute `/usr/libexec/sword/...` paths**, so any change to a shader, model, or image requires a reinstall before it has an effect. Note `sword.c` also loads `/root/models/nissan2026.glb`, which is outside the repo.

Shaders are GLSL compiled with `glslc` into `shaders/*.spv` (gitignored, rebuilt every build — the `shaders` target is `.PHONY`).

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
- `pe_window_width` / `pe_window_height` - set from `WINDOW_WIDTH` /
  `WINDOW_HEIGHT` in `wayland_window/window.h`, which is still the app's authority on the size.
  The swap chain extent, the camera and the 2D ortho projection all read them.
- `is_wayland_window` and `is_drm_rendering` are declared in
  `<engine/renderer/renderer.h>` and defined in pengine's `vulkan.c`.
  `wayland_window/window.c` and `main()` still set them.

### Generated Wayland protocol code

`desktop-server.{c,h}` (xdg-shell) and `linux-dmabuf.{c,h}` are `wayland-scanner` output but are checked into git. Regenerate with `generate_wayland_protocol_files.sh` (run from inside `source_code/`, where all of it lives now) — do not hand-edit them.

### System dependencies

pengine (`libpengine.a`, see above), vulkan (+ validation layers), libdrm, gbm, EGL/GLESv2/GL, wayland-server, wayland-client, wayland-egl, libinput, libudev, xkbcommon, libseat, lodepng (`liblodepng.a`), and the header-only cglm + cgltf.

**Neither Makefile tracks header dependencies.** Changing a header - especially
`/usr/local/include/pway/pway.h` - recompiles nothing, and `make` cheerfully
reports "Nothing to be done". A stale object linked against an older struct
layout writes its fields at the offsets it was built with: a `pway.h` change
once left `pway_window_resized()` storing the window width and height on top of
the `pway->key` function pointer, and the next keypress jumped to `0x3af00000434`
- which is just 943 x 1076, the tiled window size. After touching a header in
either repo, `make clean` (or `touch *.c`) in **every** project that includes
it: pengine, pway, sword, and pterminal. Adding a member to `PWay` goes on the **end**
of the struct for the same reason.

**pway** (`/root/pway`) supplies the window. It is a separate repo installed to `/usr/local/lib/libpway.a` and `/usr/local/include/pway`, which is why the build carries `-I/usr/local/include` and `-L/usr/local/lib`. If linking picks up a stale archive, rebuild it there with `make && make install` — the headers and the `.a` are not versioned against each other.

There is **no X11**. Sword is a Wayland client of the host compositor, or it drives DRM/KMS directly.

## Architecture

### One thread (`compositor.c`)

**Sword is single-threaded**, the way sway and the other wlroots compositors
are. It used to be three threads — a render loop on `main()`, a compositor
thread, and an input thread — with three mutexes holding them together. All of
that is gone. Any thread left in a `ps` listing belongs to Mesa (the
`swordfi:disk$0` shader-cache threads), not to sword.

`main()` does setup only — memory, `init_keyboard()`, the window or the DRM
fallback, `pe_vk_init()`, `sword_outputs_init()`, `camera_init()`,
`sword_init()` — and then calls `run_compositor()`, which is the whole rest
of the program. The socket goes up only after `pe_vk_init()`, because everything
a client touches (the quad's pipeline, its buffers, the dmabuf format table the
GPU is asked for) needs a Vulkan device; a client that connected before that
died on `vkCreateBuffer: Invalid device` and took sword with it.

`run_compositor()` creates the `wl_display`, registers the globals
(wl_compositor, xdg_wm_base, shm, linux-dmabuf, seat/input, output, data
device), sets `WAYLAND_DISPLAY`, and then loops on **one `poll()`** over:

- `wl_event_loop_get_fd()` — client requests, dispatched with
  `wl_event_loop_dispatch(loop, 0)` (zero timeout: the poll above is what waited)
- a **frame timerfd** at `FRAME_INTERVAL_NS` (~16.7ms), which replaced the render
  loop's old `usleep(16667)`. When it fires, `sword_frame_step()`
  (`sword.c`) runs `handle_focus()` → `pe_frame_draw()` → `update_delta_time()`
  → `end_frame()`
- **input**: on the pway path, `pway->fds[0]` (host connection), `[1]` (key-repeat
  timerfd) and `[3]` (paste); on bare DRM, `libinput_get_fd()`

`wl_display_run()` is this loop with the waiting and the dispatching welded
together inside libwayland; it is unrolled here so input and the frame timer can
be waited on in the same `poll()`.

**Ordering inside the loop matters.** Input is dispatched *before* the frame
step. `pway_prepare_to_read_events()` (called before the poll, as pway's
`prepare_read`/`poll`/`read_events` contract requires) leaves the connection to
the host compositor in a pending read, and nothing may touch that connection
until `pway_dispatch_events()` closes it — **rendering does**, because presenting
goes out through `VK_KHR_wayland_surface` on that very same `wl_display`. Drawing
before the dispatch wedges the connection and no input is ever read.

`init_keyboard()` runs in `main()`, before any of this. The xkb keymap is the
*compositor's* — every client that calls `wl_seat.get_keyboard` is sent it — so
it cannot live behind the libinput branch that the pway path never reaches, or
serving a client dereferences a NULL `xkb_keymap`.

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
upload still waits on the render targets' fences in `end_frame()`, and
`task_release_old_buffer()` still counts frames in flight — the GPU runs
asynchronously whatever the CPU thread count is. See **Frame path**.

### Two orthogonal mode flags

- `is_opengl` (`main.c`, default `false`) — selects the EGL/GLES path (`compositor/egl.c`, `buffers.c`) instead of Vulkan. The Vulkan path is the live one; the EGL path exits early via `goto finish`.
- `is_drm_rendering` (pengine's `renderer/vulkan.c`, set by `main()`) — set to `true` when `create_wayland_window()` fails (no compositor to connect to). Then rendering targets DRM/KMS directly (`direct_render.c`, `renderer/display.c`, `compositor.gpu_path = "/dev/dri/card0"`), the swapchain/surface setup differs, `vkGetMemoryFdKHR` is resolved for buffer export, and input comes from libinput instead of pway.

Both flags are read all over `renderer/` (pengine) and sword's own top-level `*.c` files (what used to live under `compositor/`, before it flattened into `source_code/` — see **Layer conventions**); grep for them before changing init order.

### The window (`wayland_window/window.c`)

`create_wayland_window()` calls `pway_init()` then `pway_create_window()`, and deliberately **not** `pway_init_egl()` — Vulkan takes the raw `pway_surface` / `pway_display` through `VK_KHR_wayland_surface` instead, so the EGL context pway would build is never needed.

Ordering matters twice over. `pway_init()` connects using `WAYLAND_DISPLAY`, and `run_compositor()` later overwrites that variable with sword's own socket, so the window must be created before `run_compositor()` claims that variable, or sword tries to be a client of itself. And within pway, `pway_init()` does all the real work (registry, `wl_surface`, `xdg_surface`, listeners, first commit); `pway_create_window()` only sets the title and size.

Two things differ from the old X11 surface and are easy to reintroduce: a Wayland surface does **not** support `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`, so `swap_chain.c` picks a `compositeAlpha` out of `supportedCompositeAlpha` rather than assuming one; and the windowed EGL path in `compositor/egl.c` needs a `wl_egl_window` where it used to take an X11 `Window`.

**Resizing is not implemented.** The swapchain and camera still use the hardcoded `WINDOW_WIDTH`/`WINDOW_HEIGHT`, so a tiled window renders a 1916x1040 image into whatever size the compositor actually gave it. `pway->resize` records the new size and nothing rebuilds from it yet.

### Frame path

`pe_vk_draw_frame()` (`renderer/draw.c`) → `pe_vk_start_render_pass()` → `pe_vk_draw_commands()` → **`sword_draw_scene()` (`sword.c`)**. That last function draws each Wayland client as a textured quad (`draw_surfaces()`) and the cursor on top. **To add or change something visible, edit `sword.c`**, not the renderer.

`sword_init()` (also `sword.c`) is the counterpart: `pe_2d_init()` and `cursor_init()`. `clean_sword()` must free whatever it allocates.

`pe_vk_draw_frame()` **no longer ends in `vkQueueWaitIdle()`** — it keeps
`PE_VK_FRAMES_IN_FLIGHT` frames going on per-frame and per-image fences instead
(pengine's `renderer/draw.c`). Nothing may assume the queue is drained at the end
of a frame any more: `end_frame()` waits on the targets' fences explicitly, and
only on the frames where an shm client actually redrew. The bare-DRM path is the
exception — it still waits on this frame's fence between submit and present.

**`end_frame()` (`sword.c`) is the frame's second half**, and it is where
everything that needs the GPU to be past a frame goes: `retire_collect()`, the
fence wait when an shm upload is pending, then per task
`send_frame_callback_done()`, `task_upload_shared_memory()` and
`task_release_old_buffer()`, then `wl_display_flush_clients()`.
Anything that submits to `vk_queue` on behalf of a client belongs here and
nowhere else — this is the one point in the loop where nothing is recording.

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
at.

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

Still not advertised: `zwp_primary_selection_device_manager_v1`. GDK degrades
without it, but pterminal's `pway_primary_copy()` wants it. The XML is at
`/usr/share/wayland-protocols/unstable/primary-selection/`, so it needs two
`wayland-scanner` lines in `generate_wayland_protocol_files.sh` and a near-copy
of `data_device.c` with the drag half removed.

### xdg-shell

`xdg_wm_base` is advertised at version 1 (`compositor.c`), and every
request of every xdg interface has a handler for the reason above — a NULL entry
is dispatched as a call. `xdg_toplevel` (`top_level.c`) records the
title, app id and size limits and no-ops the rest; `set_maximized`,
`unset_maximized`, `set_fullscreen` and `unset_fullscreen` **must** still answer
with a configure even though sword declines them, because a client blocks
waiting for that configure before it will draw again. `reconfigure()` sends the
size the client already has and an empty state array, which is the protocol's
way of saying no — and since the layout is what keeps `TopLevel.width/height`
up to date, that is automatically the window's own tile rather than a stale
guess. A tiled window is already the only size it is going to get, so declining
is the honest answer to all four.

The **initial** configure comes out of `layout_apply()` too, not from
`get_top_level_implementation()` itself. It is the only configure a client gets
before it is allowed to draw, so the layout has to have run by the time that
function returns.

Configure serials come from `wl_display_next_serial()` and are stored in
`DesktopSurface.pending_serial`, so `do_desktop_ack()` can tell a real ack from
a stale one. A constant serial made every configure look like the same event.

`xdg_positioner` and `xdg_popup` (`popup.c`) exist only so a client
opening a menu does not take the compositor down. There is no second quad to
draw a popup into, so `create_popup()` creates the resource and immediately
sends `xdg_popup.popup_done`. That is the only one of the three options that
leaves the client alive: a protocol error kills it, and silence hangs a client
that took a grab.

### The tiling layout (`layout.c`)

The window manager half, and deliberately **policy only**: it writes a rectangle
onto every `Task` and `draw_surface()` puts the quad where it says. Nothing in
it touches the GPU, which is what lets it run straight from the request handlers where the
requests arrive.

The layout is a **spiral**. Each window takes half of what is left and the split
direction cycles right, down, left, up, so the free area winds inward — the
oldest window keeps the biggest cell and a new one always appears beside the one
before it. `LAYOUT_SPIRAL 0` switches it to dwindle, where the free area only
ever walks toward the bottom right. The arithmetic is worth checking against a
pixel-counting harness when it changes: at one through seven windows the cells
must cover the output exactly, with no overlap and no hole. `LAYOUT_GAP` is the
space *between* two windows; every cell gives up half of it on each side and the
output starts out short of the same half, so the margin at the screen edge comes
out the same width as the gap between neighbours.

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

Shortcuts live in `handle_sword_key()` (`keyboard.c`) with the rest:
**super+j / super+k** cycle the focus, **super+c** closes the focused window —
`q` is already the compositor's own exit. `xdg_toplevel.close` is a *request*; a
client with unsaved work may put up a dialog and stay.

An **unmap** (`surface_attach()` with a NULL buffer) keeps its cell rather than
reflowing. Reflowing there costs the window its place to whoever is behind it
and hands it a different one when it maps again, which is worse until the layout
keeps a stable order of its own — the `//TODO` says so rather than leaving it
looking like an oversight.

`move`, `resize` and `show_window_menu` stay no-ops: a tiled window's size is
not the client's to ask for.

### The output (`output.c`)

One `wl_output` at version 4, and its mode is the image sword renders:
`WINDOW_WIDTH`×`WINDOW_HEIGHT` at 60000 mHz, flagged `CURRENT | PREFERRED` since
there is nothing to switch to. `send_output_state()` is the single place that
describes it, so a resize has one place to send a new mode from once the swap
chain can change size. Every event above version 1 is gated on the version the
client bound at, and `release` — the only request the interface has — has a
handler.

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

### The clipboard (`data_device.c`)

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
`mouse.c` owns the cursor position in the render target's own pixels and is fed
by both input paths — `wayland_window/window.c` wires pway's `update_mouse`/`click`/
`click_release` on the windowed path, `device_input.c` forwards libinput's pointer
events on the DRM one. `input.c` turns a cursor position into
`wl_pointer` events.

Three things about that are easy to get wrong. The host reports the cursor in
the **window's** pixels and sword renders at a fixed `WINDOW_WIDTH`
×`WINDOW_HEIGHT` that the host's own window is only scaled into, so the position
has to be scaled the same way the image is or the cursor lands somewhere other
than where the user is pointing. (That is the *outer* scale, sword inside
the host compositor's window. The layout adds an inner one — see below.) pway hands over which of *its* buttons moved rather
than an evdev code, and labels the wheel by the opposite sign convention from
the protocol's, so `pway_window_click_release()` translates both. And libinput
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
but the keyboard follows `focused_task`, which only a new window and super+j/k
ever moved — so clicking a terminal sent the pointer there and left the keys
going wherever they already went. A **press** stores `pointer_focus` in
`focused_task` and the frame step's `handle_focus()` turns that into
`wl_keyboard.leave`, `enter` and a fresh clipboard offer, exactly as it does for
`layout_focus_next()`. Only the press: a release belongs to whoever took the
press, and moving the focus on it would hand the keyboard away when the button
comes up over a different tile.

The **second scale** lives here. `draw_surface()` stretches a client's buffer
into its tile, so the buffer is not the size of the rectangle on screen and
`pointer_inside()` has to divide the position back into buffer coordinates —
a client told the cursor is at the tile's own coordinates draws its cursor
somewhere other than where the user is pointing, which is the same bug as the
outer scale one level up.

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

A client surface becomes a `Task` (`compositor.h`): it owns the `wl_resource`, the client's buffer, a `PTexture`, and a `PModel` quad — plus, once the client gives it a toplevel role, the tile the layout put it in and a listener-backed pointer to its `TopLevel`. `Task`s live in the `tasks_for_draw` array; each frame `draw_surfaces()` draws them and `end_frame()` sends the frame callbacks (`send_frame_callback_done`), uploads shm pixels, pays owed releases, and then `wl_display_flush_clients`.

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

On the **dmabuf** side the `PTexture` and its Vulkan image still leak, and the
surface goes on sampling a `PTexture` the `wl_buffer` freed: destroying it where
the request arrives would destroy an image a frame in flight is still sampling.
The shm side answers exactly that with its retire list, and the TODO in
`handle_buffer_destroyed()` says the dma side wants the same treatment.

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
  anyway. `main()` calls it only on the **bare DRM path**, in the
  `create_wayland_window()` failure branch — on the pway path the terminal is
  real and output belongs there. The mirror to stderr that the records get on
  the pway path is switched off at the same time, or the file would hold
  everything twice. stdout is put back to **line buffered** afterwards: a log
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
- **The Wayland server implementation** — `compositor.c`, `surface.c`, `tasks.c`, `top_level.c`, `data_device.c`, `dma.c`, `shared_memory.c`, and the rest of what used to live under a `compositor/` subdirectory, now flattened into `source_code/` alongside everything else: this *is* the main code, not a piece tucked away from it. It includes the one piece of window-manager *policy* (`layout.c`): which window gets which piece of the output. It is written against `Task` and `TopLevel` and sends configures, not because it draws anything.
- **`wayland_window/`** — the pway windowed dev path (`window.c`/`window.h`), off in its own directory precisely because it is a *tool* for developing without a VT switch, not the main path: bare DRM (see **Two orthogonal mode flags**) is. `device_input.c` still branches on `is_wayland_window` to pump `pway_handle_events()` instead of libinput, but the pway-specific glue itself — connecting to the host compositor, translating its mouse/keyboard callbacks — lives only here.
- Everything else at the top level — `main.c`, `device_input.c` (libinput/pway event pump), `keyboard.c`, `mouse.c`, `outputs.c`, `launch.c` (spawning a program from a keybinding), `sword.c` (compositor-side drawing: client quads, the cursor), `cursor.c`, `tty.c`, `log.c` (see **The log**).

### Memory

`engine/memory.c` allocates one 750 MB block up front (`pe_init_memory()` at startup, `clear_engine_memory()` at exit) and hands out bump/stack allocations from it. `Array` (`engine/array.h`) is the generic growable container built on top and is used everywhere instead of raw malloc'd buffers. Prefer these over `malloc` for engine data.

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
