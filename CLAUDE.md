# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Swordfish is a single C binary that is simultaneously a **Wayland compositor**, a **Vulkan renderer**, and a small **3D engine**. It displays software build progress as a 3D scene (inspired by the movie *Swordfish*): you run `swordfish <command>`, and client windows plus build output are composited into the 3D world.

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

### Compile flags per directory

`source_code/Makefile` uses different flags per subdirectory: `engine/` and `renderer/` get `-DCGLM_FORCE_DEPTH_ZERO_TO_ONE -DCGLM_FORCE_LEFT_HANDED`; top-level and `compositor/` objects do **not**. Keep cglm math in `engine/`/`renderer/` so handedness/depth conventions stay consistent.

### Generated Wayland protocol code

`compositor/desktop-server.{c,h}` (xdg-shell) and `compositor/linux-dmabuf.{c,h}` are `wayland-scanner` output but are checked into git. Regenerate with `compositor/generate_wayland_protocol_files.sh` (run from inside `compositor/`) — do not hand-edit them.

### System dependencies

vulkan (+ validation layers), libdrm, gbm, EGL/GLESv2, wayland-server, wayland-client, wayland-egl, libinput, libudev, xkbcommon, libseat, lodepng (`liblodepng.a`), and the header-only cglm + cgltf.

**Neither Makefile tracks header dependencies.** Changing a header - especially
`/usr/local/include/pway/pway.h` - recompiles nothing, and `make` cheerfully
reports "Nothing to be done". A stale object linked against an older struct
layout writes its fields at the offsets it was built with: a `pway.h` change
once left `pway_window_resized()` storing the window width and height on top of
the `pway->key` function pointer, and the next keypress jumped to `0x3af00000434`
- which is just 943 x 1076, the tiled window size. After touching a header in
either repo, `make clean` (or `touch *.c`) in **every** project that includes
it: pway, swordfish, and pterminal. Adding a member to `PWay` goes on the **end**
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

### Two orthogonal mode flags

- `is_opengl` (`main.c`, default `false`) — selects the EGL/GLES path (`compositor/egl.c`, `buffers.c`) instead of Vulkan. The Vulkan path is the live one; the EGL path exits early via `goto finish`.
- `is_drm_rendering` (`swordfish.c`) — set to `true` when `create_wayland_window()` fails (no compositor to connect to). Then rendering targets DRM/KMS directly (`direct_render.c`, `renderer/display.c`, `compositor.gpu_path = "/dev/dri/card0"`), the swapchain/surface setup differs, `vkGetMemoryFdKHR` is resolved for buffer export, and input comes from libinput instead of pway.

Both flags are read all over `renderer/` and `compositor/`; grep for them before changing init order.

### The window (`window.c`)

`create_wayland_window()` calls `pway_init()` then `pway_create_window()`, and deliberately **not** `pway_init_egl()` — Vulkan takes the raw `pway_surface` / `pway_display` through `VK_KHR_wayland_surface` instead, so the EGL context pway would build is never needed.

Ordering matters twice over. `pway_init()` connects using `WAYLAND_DISPLAY`, and `run_compositor()` later overwrites that variable with swordfish's own socket, so the window must be created before the compositor thread starts or swordfish tries to be a client of itself. And within pway, `pway_init()` does all the real work (registry, `wl_surface`, `xdg_surface`, listeners, first commit); `pway_create_window()` only sets the title and size.

Two things differ from the old X11 surface and are easy to reintroduce: a Wayland surface does **not** support `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`, so `swap_chain.c` picks a `compositeAlpha` out of `supportedCompositeAlpha` rather than assuming one; and the windowed EGL path in `compositor/egl.c` needs a `wl_egl_window` where it used to take an X11 `Window`.

**Resizing is not implemented.** The swapchain and camera still use the hardcoded `WINDOW_WIDTH`/`WINDOW_HEIGHT`, so a tiled window renders a 1916x1040 image into whatever size the compositor actually gave it. `pway->resize` records the new size and nothing rebuilds from it yet.

### Frame path

`pe_vk_draw_frame()` (`renderer/draw.c`) → `pe_vk_start_render_pass()` → `pe_vk_draw_commands()` → **`swordfish_draw_scene()` (`swordfish.c`)**. That last function is the application-level scene: it is where models are updated and recorded, and where `draw_surfaces()` renders each Wayland client as a textured quad. **To add or change something visible, edit `swordfish.c`**, not the renderer.

`swordfish_init()` (also `swordfish.c`) is the counterpart: load textures, create descriptor sets, create shaders/pipelines. `clean_swordfish()` must free whatever it allocates.

`pe_vk_draw_frame()` ends in a `vkQueueWaitIdle()` (`renderer/draw.c`), and two things silently depend on it: `wl_buffer_send_release()` in `draw_surfaces()` is queued during command recording but only flushed after the GPU has drained, and `system_monitor_draw()` / `processes_draw()` rewrite their host-visible instance buffers while the previous frame is assumed finished. Removing that wait — the obvious performance fix — means all of them need gating on the frame fence instead. They are a matched set; fix them together.

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

Two things the compositor still does **not** advertise: `wl_data_device_manager`
and `zwp_primary_selection_device_manager_v1`, so there is no clipboard. pway
tolerates their absence now, but it used to call
`wl_data_device_manager_get_data_device()` on a NULL proxy, which is a segfault
inside libwayland-client — a missing global crashes the *client*, silently, with
no protocol error to read.

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

A client surface becomes a `Task` (`compositor/compositor.h`): it owns the `wl_resource`, the client's buffer, a `PTexture`, and a `PModel` quad. Buffers arrive either through wl_shm (`compositor/shared_memory.c`) or linux-dmabuf (`compositor/dma.c`, imported into a Vulkan image via dmabuf fds). `Task`s live in the `tasks_for_draw` array; each frame `draw_surfaces()` draws them and `end_frame()` sends the frame callbacks (`send_frame_callback_done`) then `wl_display_flush_clients`.

Teardown belongs in `destroy_surface()` — the resource destructor, which holds `draw_tasks_mutex` — not in the `wl_surface.destroy` handler, which runs before the `Task` leaves `tasks_for_draw` and so can free a Vulkan image the render thread is still sampling. `task->image` is also **NULL until the first attach**, and `pe_vk_clean_image()` reads straight through the pointer: a client that creates a surface and destroys it without ever drawing used to segfault the compositor.

### Layer conventions

- **`renderer/`** — thin Vulkan wrapper, everything prefixed `pe_vk_`. `pe_vk_init()` in `renderer/vulkan.c` is the authoritative, order-sensitive init sequence; `pe_vk_end()` is its mirror.
- **`engine/`** — reusable engine ("pengine", hence the `pe_` prefix): custom allocator, `Array` container, glTF/PNG loading, camera, 2D/text.
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
