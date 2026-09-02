# Sword

Sword is a **Wayland compositor** with a **Vulkan renderer**: a tiling
window manager that draws every client as a textured quad. The renderer lives
in a separate repo, [pengine](https://github.com/oscar0pavon/pengine), linked
in as `libpengine.a`.

It used to be more than that — one binary that was also a small 3D engine,
compositing client windows into a 3D scene of the machine itself (inspired by
the movie *Sword*). That scene moved out to its own repo,
[3dtop](https://github.com/oscar0pavon/3dtop), because the tiling layout covers
the whole output and the scene was never actually visible behind the windows.
As a standalone Wayland client it is, and it now doubles as sword's own
test client.

Sword drives **DRM/KMS directly** from a tty. There is no nested mode: the
development path that ran it as a window inside another compositor was removed
once sword became the thing being used rather than the thing being tested.

It is a daily driver. Firefox, Thunar, GIMP and pwvucontrol all run under it,
alongside pterminal, pmenu and 3dtop.

**Multimonitor**: one output per display, laid left to right into a single
virtual desktop that the tiling layout, the `wl_output` protocol and the cursor
all speak in. A monitor mounted in portrait can be rotated 90° with
`SWORD_OUTPUT_ROTATE` — a comma-separated list of output indices. The rotation
is done in software, once per quad, because mesa's `wsi_display` offers no
display transform to ask for.

**Windows** are tiled in a spiral: each new one takes half of what is left and
the split direction cycles, so the oldest window keeps the biggest cell and a
new one always appears beside the one before it. Any window can be flipped
floating, and a floating one can be dragged, including from one monitor to
another.

**Clients** get xdg-shell with working popup menus, subsurfaces, input regions,
a keyboard, a pointer, a clipboard and a primary selection. Buffers arrive
either through `wl_shm` or `linux-dmabuf`; a dmabuf is sampled zero-copy out of
the client's own memory, an shm buffer is copied to the GPU each time the
client redraws.

Not done yet: **XWayland** (an X11 client cannot run at all), **workspaces**,
**resizing** (the swapchain and the tiles are a fixed size, so there is no
monitor hotplug either), **layer-shell**, and **damage tracking** — every frame
is a full redraw.

# Dependencies

- a C compiler and `make`
- vulkan drivers, headers and validation layers
- `glslc` (shaderc) to compile the GLSL shaders
- wayland-server and `wayland-scanner`
- libdrm
- libinput, libudev — input on the DRM path
- xkbcommon
- lodepng (`liblodepng.a`), and the header-only cglm and cgltf
- [pengine](https://github.com/oscar0pavon/pengine) — the engine and the Vulkan
  renderer, installed to `/usr/local/lib/libpengine.a`
- [pway](https://github.com/oscar0pavon/pway) — no longer used by sword's own
  code, but still on the link line to resolve symbols in `libpengine.a`

There is **no X11**.

The xdg-shell and linux-dmabuf protocol code is generated with
`wayland-scanner` and checked into git, so it does not have to be regenerated
to build. To regenerate it, from inside `source_code/`:

```
./generate_wayland_protocol_files.sh
```

# Build

The repo path is hardcoded in the build, so it has to live at `/root/sword`.

    make
    sudo make install

`make` on its own builds and then installs. The binary reads its shaders from
absolute paths under `/usr/libexec/sword`, so a shader change needs a reinstall
before it has any effect.

Run it from a tty with no arguments. It drives every connected display.

## Keys

Every shortcut is behind **super**, so everything else goes to the focused
client.

| key | |
|---|---|
| `super` + `enter` | open a terminal |
| `super` + `m` | open firefox |
| `super` + `d` | open the launcher |
| `super` + `h` / `l` | cycle focus to the previous / next window |
| `super` + `c` | close the focused window |
| `super` + `space` | toggle the focused window floating |
| `super` + `shift` + `c` | quit sword |
| `super` + `w` | switch virtual terminal |

`ctrl` + `alt` + `F1`…`F12` switches virtual terminal too — sword performs the
switch itself, because putting the console keyboard in `K_OFF` stops the kernel
from answering that combination on its own.

# Logging

There is no terminal to read on a tty, so sword writes `/tmp/sword.log`
(override with `$SWORD_LOG`), keeping the previous run as `.old`.
`SWORD_LOG_LEVEL=debug|info|warn|error` filters it, and `SWORD_LOG_SYNC=1`
flushes every record — which is what you want when the failure being chased
locks the machine rather than crashing the process.
