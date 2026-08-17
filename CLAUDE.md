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

vulkan (+ validation layers), libdrm, gbm, EGL/GLESv2, X11, wayland-server, libinput, libudev, xkbcommon, libseat, lodepng (`liblodepng.a`), and the header-only cglm + cgltf.

## Architecture

### Threads (`main.c`)

Three threads, started in `main()`:

1. **Main thread** — Vulkan render loop: `handle_focus()` → `pe_vk_draw_frame()` → `usleep(16667)` → `update_delta_time()` → `end_frame()`.
2. **Compositor thread** — `run_compositor()` creates the `wl_display`, registers globals (wl_compositor, xdg_wm_base, shm, linux-dmabuf, seat/input), sets `WAYLAND_DISPLAY`, and blocks in `wl_display_run()`.
3. **Input thread** — `handle_input()`; X11 `XNextEvent` loop when running in a window, libinput/udev when running on bare DRM.

`draw_tasks_mutex` and `focus_task_mutex` (`swordfish.c`) guard the `tasks_for_draw` array shared between the compositor thread and the render loop. Anything touching client surfaces from the render side must take `draw_tasks_mutex`.

### Two orthogonal mode flags

- `is_opengl` (`main.c`, default `false`) — selects the EGL/GLES path (`compositor/egl.c`, `buffers.c`) instead of Vulkan. The Vulkan path is the live one; the EGL path exits early via `goto finish`.
- `is_drm_rendering` (`swordfish.c`) — set to `true` when `create_window()` fails (no X display). Then rendering targets DRM/KMS directly (`direct_render.c`, `renderer/display.c`, `compositor.gpu_path = "/dev/dri/card0"`), the swapchain/surface setup differs, `vkGetMemoryFdKHR` is resolved for buffer export, and input comes from libinput instead of X.

Both flags are read all over `renderer/` and `compositor/`; grep for them before changing init order.

### Frame path

`pe_vk_draw_frame()` (`renderer/draw.c`) → `pe_vk_start_render_pass()` → `pe_vk_draw_commands()` → **`swordfish_draw_scene()` (`swordfish.c`)**. That last function is the application-level scene: it is where models are updated and recorded, and where `draw_surfaces()` renders each Wayland client as a textured quad. **To add or change something visible, edit `swordfish.c`**, not the renderer.

`swordfish_init()` (also `swordfish.c`) is the counterpart: load textures, create descriptor sets, create shaders/pipelines. `clean_swordfish()` must free whatever it allocates.

### The city (`city.c`)

The main scene content. `city_init()` does one `readdir()` pass over a directory and turns every entry into a `PInstance` (position, scale, colour, packed name); `city_draw()` renders the whole skyline with a **single** `vkCmdDrawIndexed`. Directory entries become towers — height from child count for directories, from file size for files, both log-scaled.

Facades are drawn entirely in `shaders/city.frag`, which samples `font.png` as a 16×16 atlas indexed straight by ASCII code. Two things about that atlas are easy to get wrong: glyphs live in the **alpha** channel, and atlas row 0 is the **top** of the PNG while a wall counts `v` from the ground up, so `glyph_alpha()` reads the glyph back with `1.0 - local.y`.

Each building shows its real filename on a repeating horizontal band. The name is packed 4 characters per uint into the instance data (`PINSTANCE_NAME_MAX`, 32) rather than going through a storage buffer, which is why no extra descriptor layout was needed. Tuning constants sit at the top of `city.frag` (`NAME_CHAR_WIDTH`, `NAME_BAND_SPACING`) and in `city.c` (`CITY_FOOTPRINT`, `CITY_STREET_WIDTH`).

Note `array_add` does **not** grow an array — it hits `debug_break()` on overflow — so `city.buildings` is capped at `CITY_MAX_BUILDINGS` and sized up front.

### Wayland client → 3D quad

A client surface becomes a `Task` (`compositor/compositor.h`): it owns the `wl_resource`, the client's buffer, a `PTexture`, and a `PModel` quad. Buffers arrive either through wl_shm (`compositor/shared_memory.c`) or linux-dmabuf (`compositor/dma.c`, imported into a Vulkan image via dmabuf fds). `Task`s live in the `tasks_for_draw` array; each frame `draw_surfaces()` draws them and `end_frame()` sends the frame callbacks (`send_frame_callback_done`) then `wl_display_flush_clients`.

### Layer conventions

- **`renderer/`** — thin Vulkan wrapper, everything prefixed `pe_vk_`. `pe_vk_init()` in `renderer/vulkan.c` is the authoritative, order-sensitive init sequence; `pe_vk_end()` is its mirror.
- **`engine/`** — reusable engine ("pengine", hence the `pe_` prefix): custom allocator, `Array` container, glTF/PNG loading, camera, 2D/text.
- **`compositor/`** — Wayland server implementation.
- Top-level `*.c` — app glue: window/X11, input, keyboard/xkb, child-process build invocation (`build.c`), the scene (`swordfish.c`), and the directory city (`city.c`).

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
