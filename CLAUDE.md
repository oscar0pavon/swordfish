# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Swordfish is a single C binary that is simultaneously a **Wayland compositor**, a **Vulkan renderer**, and a small **3D engine**. The renderer and the engine are no longer in this repo: they are **pengine** (`/root/pengine`), linked in as `libpengine.a`. What is left here is the compositor and the scene. It displays software build progress as a 3D scene (inspired by the movie *Swordfish*): you run `swordfish <command>`, and client windows plus build output are composited into the 3D world.

## Build / install

The repo path is hardcoded in the build (`-I/root/swordfish/source_code` in `source_code/Makefile`, and in `generate_compile_commands.sh`), so it must live at `/root/swordfish`.

```sh
make                  # root Makefile: builds AND runs `make install` (needs root)
make -C source_code   # build only -> ./swordfish
make clean            # removes binary, *.o, ../shaders/*.spv
./source_code/generate_compile_commands.sh   # regenerate compile_commands.json for clangd
```

`make install` copies the binary to `/usr/bin` and `shaders/`, `models/`, `images/*` to `/usr/libexec/swordfish/`. **Assets are loaded at runtime from absolute `/usr/libexec/swordfish/...` paths**, so any change to a shader, model, or image requires a reinstall before it has an effect. Note `swordfish.c` also loads `/root/models/nissan2026.glb`, which is outside the repo.

Shaders are GLSL compiled with `glslc` into `shaders/*.spv` (gitignored, rebuilt every build — the `shaders` target is `.PHONY`).

There is no test suite and no linter.

### pengine

`engine/` and `renderer/` used to be subdirectories here. They are now
`/root/pengine/src/engine` and `/root/pengine/src/engine/renderer`, built into
`/usr/local/lib/libpengine.a` by `make && make install` in that repo. **A change
to anything under `renderer/` or `engine/` is a change to pengine**, and needs a
rebuild and reinstall there before swordfish sees it.

`make install` in pengine copies the header tree to `/usr/local/include/pengine`
keeping the `engine/...` prefix it is written against, so swordfish compiles
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
into pengine's `include.make`. They apply to pengine only; swordfish's own
objects are built without them, exactly as before. Keep cglm math on pengine's
side so the depth and handedness conventions stay consistent.

The shm upload (`compositor/shared_memory.c`) assembles its own version of
`pe_vk_create_texture_from_image()` out of `pe_vk_transition_image_layout()`,
`pe_vk_image_copy_buffer()` and `pe_vk_create_texture_sampler()`, because that
function hardcodes `VK_FORMAT_R8G8B8A8_SRGB` and takes a `PImage`. The three were
already external; they only had to be **declared** in pengine's `vk_images.h`.
All of them end in `vkQueueWaitIdle` — see **Frame path** for which thread may
call them.

### What swordfish hands the renderer

pengine cannot call `swordfish_draw_scene()` by name any more, and three globals
that used to be swordfish's now belong to the renderer. `main()` wires them up
before `pe_vk_init()`:

- `pe_vk_draw_scene` - a function pointer, set to `swordfish_draw_scene`.
  `pe_vk_draw_commands()` calls it in the middle of the render pass.
- `pe_window_width` / `pe_window_height` - set from `WINDOW_WIDTH` /
  `WINDOW_HEIGHT` in `window.h`, which is still the app's authority on the size.
  The swap chain extent, the camera and the 2D ortho projection all read them.
- `is_wayland_window` and `is_drm_rendering` are declared in
  `<engine/renderer/renderer.h>` and defined in pengine's `vulkan.c`.
  `window.c` and `main()` still set them.

### Generated Wayland protocol code

`compositor/desktop-server.{c,h}` (xdg-shell) and `compositor/linux-dmabuf.{c,h}` are `wayland-scanner` output but are checked into git. Regenerate with `compositor/generate_wayland_protocol_files.sh` (run from inside `compositor/`) — do not hand-edit them.

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
it: pengine, pway, swordfish, and pterminal. Adding a member to `PWay` goes on the **end**
of the struct for the same reason.

**pway** (`/root/pway`) supplies the window. It is a separate repo installed to `/usr/local/lib/libpway.a` and `/usr/local/include/pway`, which is why the build carries `-I/usr/local/include` and `-L/usr/local/lib`. If linking picks up a stale archive, rebuild it there with `make && make install` — the headers and the `.a` are not versioned against each other.

There is **no X11**. Swordfish is a Wayland client of the host compositor, or it drives DRM/KMS directly.

## Architecture

### Threads (`main.c`)

Three threads, started in `main()`:

1. **Main thread** — Vulkan render loop: `handle_focus()` → `pe_vk_draw_frame()` → `usleep(16667)` → `update_delta_time()` → `end_frame()`.
2. **Compositor thread** — started *after* `pe_vk_init()` and `swordfish_init()`, because everything a client touches on it (the quad's pipeline, its buffers, the dmabuf format table the GPU is asked for) needs a Vulkan device; a client that connected before that died on `vkCreateBuffer: Invalid device` and took swordfish with it. `run_compositor()` creates the `wl_display`, registers globals (wl_compositor, xdg_wm_base, shm, linux-dmabuf, seat/input), sets `WAYLAND_DISPLAY`, and blocks in `wl_display_run()`.
3. **Input thread** — `handle_input()`; when there is a pway window it loops on `pway_handle_events()`, otherwise libinput/udev on bare DRM. `pway_handle_events()` polls with an infinite timeout, so it must stay on its own thread — calling it from the render loop would stall rendering until something happened. Pumping it is also what gets the xdg surface configured, so the window never maps without it.

`init_keyboard()` runs in `main()`, not on the input thread. The xkb keymap is
the *compositor's* — every client that calls `wl_seat.get_keyboard` is sent it —
so it cannot live behind the libinput branch that the pway path returns before
reaching, or serving a client dereferences a NULL `xkb_keymap`.

`draw_tasks_mutex` and `focus_task_mutex` (`swordfish.c`) guard the `tasks_for_draw` array shared between the compositor thread and the render loop. Anything touching client surfaces from the render side must take `draw_tasks_mutex`.

#### Sending to a client is single-threaded — `lock_wayland()`

**libwayland-server has no locking of its own**, and all three threads send
events: the compositor thread out of its event loop, the render thread in
`end_frame()` and `draw_surfaces()`, the input thread in `send_wayland_key()`
and the pointer. Everything a client is sent goes through one ring buffer per
connection whose head a send advances and whose tail a flush moves, so two
threads at once lose an update and leave the tail *past* the head. The client
then reads a message length out of the middle of an event and drops the
connection — from the outside it looks exactly like the compositor closing it:

```
Data too big for buffer (18446744073709117440 + 8 > 4096).   # -434176, from libwayland's ring buffer
Message length 19200 exceeds limit 4096                       # printed by the *client*
error in client communication (pid ...)
```

`lock_wayland()` / `unlock_wayland()` (`compositor/compositor.c`) serialise it.
The compositor thread holds it **across the whole dispatch**, which is why
`run_compositor()` no longer calls `wl_display_run()`: that welds the waiting to
the dispatching, leaving nowhere to hold a lock. Unrolled, it polls
`wl_event_loop_get_fd()` without the lock and calls
`wl_event_loop_dispatch(loop, 0)` with it. Holding it across the dispatch is
what covers the events **libwayland itself** sends — `wl_display.delete_id`
after every `wl_resource_destroy`, protocol errors — which no lock around our
own calls could reach.

It is **recursive** (a handler cannot know whether it was reached from the
dispatch or from the render thread) and it is the **outermost lock**: take it
before `draw_tasks_mutex` and `focus_task_mutex`, never after, or the render
thread and a request handler deadlock over the pair.

Code already running on the compositor thread — every request handler — is
inside it and needs nothing. Only the render and input threads take it by hand.

### Two orthogonal mode flags

- `is_opengl` (`main.c`, default `false`) — selects the EGL/GLES path (`compositor/egl.c`, `buffers.c`) instead of Vulkan. The Vulkan path is the live one; the EGL path exits early via `goto finish`.
- `is_drm_rendering` (pengine's `renderer/vulkan.c`, set by `main()`) — set to `true` when `create_wayland_window()` fails (no compositor to connect to). Then rendering targets DRM/KMS directly (`direct_render.c`, `renderer/display.c`, `compositor.gpu_path = "/dev/dri/card0"`), the swapchain/surface setup differs, `vkGetMemoryFdKHR` is resolved for buffer export, and input comes from libinput instead of pway.

Both flags are read all over `renderer/` and `compositor/`; grep for them before changing init order.

### The window (`window.c`)

`create_wayland_window()` calls `pway_init()` then `pway_create_window()`, and deliberately **not** `pway_init_egl()` — Vulkan takes the raw `pway_surface` / `pway_display` through `VK_KHR_wayland_surface` instead, so the EGL context pway would build is never needed.

Ordering matters twice over. `pway_init()` connects using `WAYLAND_DISPLAY`, and `run_compositor()` later overwrites that variable with swordfish's own socket, so the window must be created before the compositor thread starts or swordfish tries to be a client of itself. And within pway, `pway_init()` does all the real work (registry, `wl_surface`, `xdg_surface`, listeners, first commit); `pway_create_window()` only sets the title and size.

Two things differ from the old X11 surface and are easy to reintroduce: a Wayland surface does **not** support `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`, so `swap_chain.c` picks a `compositeAlpha` out of `supportedCompositeAlpha` rather than assuming one; and the windowed EGL path in `compositor/egl.c` needs a `wl_egl_window` where it used to take an X11 `Window`.

**Resizing is not implemented.** The swapchain and camera still use the hardcoded `WINDOW_WIDTH`/`WINDOW_HEIGHT`, so a tiled window renders a 1916x1040 image into whatever size the compositor actually gave it. `pway->resize` records the new size and nothing rebuilds from it yet.

### Frame path

`pe_vk_draw_frame()` (`renderer/draw.c`) → `pe_vk_start_render_pass()` → `pe_vk_draw_commands()` → **`swordfish_draw_scene()` (`swordfish.c`)**. That last function is the application-level scene: it is where models are updated and recorded, and where `draw_surfaces()` renders each Wayland client as a textured quad. **To add or change something visible, edit `swordfish.c`**, not the renderer.

`swordfish_init()` (also `swordfish.c`) is the counterpart: load textures, create descriptor sets, create shaders/pipelines. `clean_swordfish()` must free whatever it allocates.

`pe_vk_draw_frame()` ends in a `vkQueueWaitIdle()` (`renderer/draw.c`), and several things silently depend on it: `wl_buffer_send_release()` in `draw_surfaces()` is queued during command recording but only flushed after the GPU has drained, and `system_monitor_draw()` / `processes_draw()` rewrite their host-visible instance buffers while the previous frame is assumed finished. Removing that wait — the obvious performance fix — means all of them need gating on the frame fence instead. They are a matched set; fix them together.

**`end_frame()` (`swordfish.c`) is the frame's second half**, and it is where
everything that needs an idle GPU goes: `shared_memory_collect_textures()`, then
per task `send_frame_callback_done()`, `task_upload_shared_memory()` and
`task_release_old_buffer()`, then `wl_display_flush_clients()`. It holds
`lock_wayland()` around the lot and `draw_tasks_mutex` inside it, in that order.
Anything that submits to `vk_queue` on behalf of a client belongs here and
nowhere else — this is the only point where the render thread owns the queue and
is not recording.

### Scene layout

The world is centred on the CPU: a square die at the origin (`system_monitor.c`), ringed by a city (`processes.c` by default, `city.c` for the directory instead). The camera in `main.c` is unused — `swordfish_update_camera()` in `swordfish.c` owns it and rewrites it every frame.

### The city (`city.c`)

The directory view, kept as an alternative to the process ring — `swordfish_init()` calls `processes_init()`, and swapping in `city_init(&city, ".")` puts the directory in the same ring. `city_init()` does one `readdir()` pass and turns every entry into a `PInstance` (position, scale, colour, packed name); `city_draw()` renders the whole skyline with a **single** `vkCmdDrawIndexed`. Directory entries become towers — height from child count for directories, from file size for files, both log-scaled. They are placed on concentric rings from `CITY_INNER_RADIUS` outwards; each ring's occupants are spread over the **whole** circle rather than packed along an arc, so the centre stays surrounded even with only a handful of entries.

Facades are drawn entirely in `shaders/city.frag`, which samples `font.png` as a 16×16 atlas indexed straight by ASCII code. Two things about that atlas are easy to get wrong: glyphs live in the **alpha** channel, and atlas row 0 is the **top** of the PNG while a wall counts `v` from the ground up, so `glyph_alpha()` reads the glyph back with `1.0 - local.y`.

Each building shows its real filename on a repeating horizontal band. The name is packed 4 characters per uint into the instance data (`PINSTANCE_NAME_MAX`, 32) rather than going through a storage buffer, which is why no extra descriptor layout was needed. Tuning constants sit at the top of `city.frag` (`NAME_CHAR_WIDTH`, `NAME_BAND_SPACING`) and in `city.c` (`CITY_FOOTPRINT`, `CITY_INNER_RADIUS`). `NAME_CHAR_WIDTH` is a **ceiling**, not a size: a name too long for its facade shrinks to fit, so short names render large. The random glyph noise on the rest of the wall must stay well below the name's brightness or it reads as texture over the text.

Note `array_add` does **not** grow an array — it hits `debug_break()` on overflow — so `city.buildings` is capped at `CITY_MAX_BUILDINGS` and sized up front.

`city_create_box()` is the unit box every instanced tower is a copy of — centred on X and Y, spanning 0..1 on Z so scaling z grows it off the ground. It is shared by the system monitor and the process ring rather than duplicated.

### The system monitor (`system_monitor.c`)

The machine's CPUs as one square die at the centre of the world, every core inside it, one instanced draw call for the lot. Reuses `city_create_box()` and the `city.vert`/`city.frag` pipeline unchanged — a monitor tower is a city building with a different instance array, so core names go through the same 4-chars-per-uint packing.

The instance array holds the cores, then `MONITOR_MEMORY_TOWERS` memory towers (`/proc/meminfo`, fractions of total), then one flat slab as the die itself. The grid is `ceil(sqrt(core_count))` columns wide.

**Height is utilisation, colour is temperature** — two independent channels, so a cool busy core and a hot idle one look different. `coretemp` numbers its sensors sparsely and labels them by *physical* core id, so `monitor_map_temperatures()` walks `temp*_label` for `Core N` and resolves each logical CPU through `/sys/devices/system/cpu/cpuN/topology/core_id`. Hyperthread siblings correctly share one sensor.

A sampler thread reads `/proc/stat` every `MONITOR_SAMPLE_INTERVAL_US` (500ms) and writes busy fractions under `sample_mutex`; the render loop copies that snapshot and eases `displayed[]` toward it, so towers move smoothly instead of snapping twice a second. Sampling at frame rate would burn the CPU this is meant to be measuring. Two details in the parse are easy to get wrong: the first `cpu` line is the machine-wide total and must be skipped, and `guest`/`guest_nice` are already counted inside `user`/`nice`, so the total stops at `steal`.

Unlike the city's, the monitor's instance buffer is rewritten every frame via `pe_vk_update_buffer()`.

The camera must stay **above `MONITOR_MAX_HEIGHT`**: below it, a busy core near the camera rises past the tops of the cores behind it and hides them entirely.

### The process ring (`processes.c`)

The running processes as the city around the die, and the only part of the scene with birth and death — the reason it reads as a monitor rather than an animated gauge.

Height is CPU (Δ`utime+stime` ÷ Δ total jiffies × core count, a fraction of **one** core), colour is resident memory on a log scale washed toward white by CPU, so the tall towers are also the bright readable ones. Name is `comm`.

The thing that matters here is **stable identity**: each of the `PROCESS_MAX` slots is given its ring position once at startup and keeps it. `process_slot_for()` maps pid → slot with a free list, so an exiting process frees its slot for reuse and nothing shuffles. Rebuilding the array by index each sample would make the whole ring lurch every time anything died.

A dead slot sets `scale.z = 0` — a degenerate box that rasterises nothing — so the draw count stays constant at `PROCESS_MAX`. Live-but-idle processes keep `PROCESS_MIN_HEIGHT`, which is what makes the idle carpet.

Parsing `/proc/<pid>/stat`: `comm` can contain spaces and brackets, so the name is taken between the **first** `(` and the **last** `)`, and the numeric fields are read from after that.

Kernel threads are skipped on `vsize == 0` — they have no address space. On this machine that is 426 of 469 entries, and none of them ever move, so leaving them in buried the interesting processes under a carpet. `vsize == 0` and an empty `/proc/<pid>/cmdline` were verified to agree on every process; `vsize` wins because it is already parsed and costs no extra syscall. Filtering also keeps the live count well under `PROCESS_MAX`, which is a hard cap (`array_add` does not grow).

Because `process_slot_for()` takes the lowest free slot and low slots are the innermost ring, live processes stay clustered around the die instead of scattering across sparse outer rings.

### The HUD (`hud.c`)

The flat overlay carrying the numbers the 3D scene can only suggest: aggregate CPU, hottest core, memory, process count, busiest process. The 3D carries shape, the HUD carries precision.

It reuses `pe_2d_get_character_uvs()` and `pe_2d_init_vulkan_buffers()` but **not** `pe_2d_create_text_geometry()`, which is single-line, fixed-string, and allocates fresh Vulkan buffers on every call — fine once at startup, a steady leak at the HUD's twice-a-second refresh. Instead the quad set is allocated once at `HUD_MAX_CHARS` and rewritten in place through `pe_vk_update_buffer()`; characters past the end of the string collapse to a single point, so `index_array.count` (which is what `pe_vk_draw_model()` passes to `vkCmdDrawIndexed`) never changes.

`shaders/hud.frag` exists because `texture.frag` returns the sampled RGB, and `font.png` keeps its glyphs in the **alpha** channel — reusing it would have drawn black on black. The HUD shader treats the sample as a mask and supplies its own colour.

The HUD reads only `displayed_*` fields from the monitor and the process ring. Those are render-thread-only by construction, so it never takes the samplers' locks.

### Advertised global versions

`COMPOSITOR_VERSION` and `SEAT_VERSION` (`compositor/compositor.h`) are what
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
`compositor/surface.c` since the quad samples the whole buffer regardless. Child
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
button release — so a click in a client looked like swordfish closing it (both
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
prints. The absence shows up as a *gap in swordfish's own log*: every bind
handler prints, so the missing line is the diagnosis.

Still not advertised: `zwp_primary_selection_device_manager_v1`. GDK degrades
without it, but pterminal's `pway_primary_copy()` wants it. The XML is at
`/usr/share/wayland-protocols/unstable/primary-selection/`, so it needs two
`wayland-scanner` lines in `generate_wayland_protocol_files.sh` and a near-copy
of `data_device.c` with the drag half removed.

### xdg-shell

`xdg_wm_base` is advertised at version 1 (`compositor/compositor.c`), and every
request of every xdg interface has a handler for the reason above — a NULL entry
is dispatched as a call. `xdg_toplevel` (`compositor/top_level.c`) records the
title, app id and size limits and no-ops the rest; `set_maximized`,
`unset_maximized`, `set_fullscreen` and `unset_fullscreen` **must** still answer
with a configure even though swordfish declines them, because a client blocks
waiting for that configure before it will draw again. `reconfigure()` sends the
size the client already has and an empty state array, which is the protocol's
way of saying no.

Configure serials come from `wl_display_next_serial()` and are stored in
`DesktopSurface.pending_serial`, so `do_desktop_ack()` can tell a real ack from
a stale one. A constant serial made every configure look like the same event.

`xdg_positioner` and `xdg_popup` (`compositor/popup.c`) exist only so a client
opening a menu does not take the compositor down. There is no second quad to
draw a popup into, so `create_popup()` creates the resource and immediately
sends `xdg_popup.popup_done`. That is the only one of the three options that
leaves the client alive: a protocol error kills it, and silence hangs a client
that took a grab.

### The output (`compositor/output.c`)

One `wl_output` at version 4, and its mode is the image swordfish renders:
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

### The clipboard (`compositor/data_device.c`)

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
`keyboard_focus` both happen under `lock_wayland()`, so it adds no lock.

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
input event the client is claiming the clipboard for, and swordfish does not keep
its serials long enough to tell — refusing on one it cannot verify would mean no
clipboard at all.

### The pointer

The seat advertises `WL_SEAT_CAPABILITY_POINTER` alongside the keyboard.
`mouse.c` owns the cursor position in the render target's own pixels and is fed
by both input paths — `window.c` wires pway's `update_mouse`/`click`/
`click_release` on the windowed path, `input.c` forwards libinput's pointer
events on the DRM one. `compositor/input.c` turns a cursor position into
`wl_pointer` events.

Three things about that are easy to get wrong. The host reports the cursor in
the **window's** pixels and swordfish renders at a fixed `WINDOW_WIDTH`
×`WINDOW_HEIGHT` that a tiled window is only scaled into, so the position has
to be scaled the same way the image is or the cursor lands somewhere other than
where the user is pointing. pway hands over which of *its* buttons moved rather
than an evdev code, and labels the wheel by the opposite sign convention from
the protocol's, so `pway_window_click_release()` translates both. And libinput
sends the deprecated `LIBINPUT_EVENT_POINTER_AXIS` **as well as** the typed
`..._SCROLL_WHEEL`/`_FINGER`/`_CONTINUOUS` events, so handling both counts every
scroll twice.

`pointer_hit_task()` is the whole hit test: every client quad is drawn in screen
space at the top left corner by `draw_surface()`, so it is the focused task's
own rectangle. When the quads move into the 3D world this becomes a ray cast and
it is the only thing that has to change.

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

Client buffers arrive as a DRM fourcc, and `compositor/drm_format.c` is the only
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

The advertised format table in `compositor/feedback.c` is built once at first
`get_default_feedback` by asking the GPU, via
`vkGetPhysicalDeviceFormatProperties2` + `VkDrmFormatModifierPropertiesListEXT`,
which modifiers it can sample each format with — it used to be two hardcoded
guesses with modifier 0. On this machine that is 16 pairs, and clients pick an
AMD DCC modifier rather than linear.

### Wayland client → 3D quad

A client surface becomes a `Task` (`compositor/compositor.h`): it owns the `wl_resource`, the client's buffer, a `PTexture`, and a `PModel` quad. `Task`s live in the `tasks_for_draw` array; each frame `draw_surfaces()` draws them and `end_frame()` sends the frame callbacks (`send_frame_callback_done`), uploads shm pixels, pays owed releases, and then `wl_display_flush_clients`.

#### Both buffer protocols make the same struct

Buffers arrive either through wl_shm (`compositor/shared_memory.c`) or
linux-dmabuf (`compositor/dma.c`). **Both put a `ClientBuffer`
(`compositor/client_buffer.h`) behind the `wl_buffer`'s user data**, tagged with
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

**The copy runs on the render thread, out of `end_frame()`.**
`pe_vk_end_single_time_cmd()` submits to `vk_queue` and waits on it, and a
`VkQueue` is not something two threads may touch at once — doing the copy where
the commit arrives would have the compositor thread submitting alongside the
render loop. `end_frame()` is the one point in the frame where the queue is idle
and nothing is recording. The cost is that shm pixels land in the *next* frame:
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
  because the client can destroy a buffer while the render thread is recording
  with its image.

Three more things `shared_memory.c` used to get wrong, all worth not
reintroducing: `wl_shm.format` was never sent at all (`wl_display_add_shm_format()`
feeds libwayland's *own* shm implementation, the one `wl_display_init_shm()`
creates, which swordfish does not use — the events go out on bind now, since a
client that binds later would hear nothing); destroying a pool munmapped and
freed it without destroying the resource, so the next request read freed memory,
and the protocol says the mapping outlives the pool until the last buffer cut
from it is gone (reference counted now); and the pool's fd was never closed.

Teardown belongs in `destroy_surface()` — the resource destructor, which holds `draw_tasks_mutex` — not in the `wl_surface.destroy` handler, which runs before the `Task` leaves `tasks_for_draw` and so can free a Vulkan image the render thread is still sampling. `task->image` is also **NULL until the first attach**, and `pe_vk_clean_image()` reads straight through the pointer: a client that creates a surface and destroys it without ever drawing used to segfault the compositor. That function destroys the **view before the image** — the other order is a validation error, since an image cannot go while a view of it exists.

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
earliest safe point: `pe_vk_draw_frame()` has already run its `vkQueueWaitIdle()`,
so nothing on the GPU is still reading the old image. Releasing inside
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
the request arrives would destroy an image the render thread is recording with.
The shm side answers exactly that with its retire list, and the TODO in
`handle_buffer_destroyed()` says the dma side wants the same treatment.

### Layer conventions

- **`renderer/`** (in pengine) — thin Vulkan wrapper, everything prefixed `pe_vk_`. `pe_vk_init()` in `renderer/vulkan.c` is the authoritative, order-sensitive init sequence; `pe_vk_end()` is its mirror.
- **`engine/`** (in pengine) — reusable engine (hence the `pe_` prefix): custom allocator, `Array` container, glTF/PNG loading, camera, 2D/text.
- **`compositor/`** — Wayland server implementation.
- Top-level `*.c` — app glue: the pway window (`window.c`), input, keyboard/xkb, child-process build invocation (`build.c`), the scene (`swordfish.c`), the directory city (`city.c`), the process ring (`processes.c`), the CPU monitor (`system_monitor.c`), and the 2D overlay (`hud.c`).

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

Follow the surrounding code: 2-space indent, `snake_case`, designated initializers for Vulkan structs, `LOG(...)`/`printf` for diagnostics, `VKVALID(call, "message")` for Vulkan results. Headers use `#ifndef NAME_H` guards. Comments are sparse; `//INFO` and `//TODO` mark notes the author cares about — leave them in place.
