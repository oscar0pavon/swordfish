# Swordfish

Swordfish is a **Wayland compositor** with a **Vulkan renderer**: a tiling
window manager that draws every client as a textured quad. The renderer lives
in a separate repo, [pengine](https://github.com/oscar0pavon/pengine), linked
in as `libpengine.a`.

It used to be more than that — one binary that was also a small 3D engine,
compositing client windows into a 3D scene of the machine itself (inspired by
the movie *Swordfish*). That scene moved out to its own repo,
[3dtop](https://github.com/oscar0pavon/3dtop), because the tiling layout covers
the whole output and the scene was never actually visible behind the windows.
As a standalone Wayland client it is, and it now doubles as swordfish's own
test client.

It runs two ways: as a **Wayland client** of another compositor (a window
inside sway), or **directly on DRM/KMS** from a tty with no compositor under it
at all. Which one is chosen automatically, by whether a host compositor
answers.

**Multimonitor** is supported on the DRM path: one output per display, laid
left to right into a single virtual desktop that the tiling layout, the
`wl_output` protocol and the cursor all speak in.

The seat carries a keyboard, a pointer and a clipboard. The pointer works out
which client the cursor is over and sends it motion, buttons and scroll, from
the host compositor in a window or from libinput on bare DRM.

Not done yet: resizing (the swapchain and the tiles are a fixed size),
floating windows (everything is tiled), popups (a client asking for one is
told no rather than shown one), and screen rotation for a monitor mounted in
portrait.

# Dependencies

- a C compiler and `make`
- vulkan drivers, headers and validation layers
- `glslc` (shaderc) to compile the GLSL shaders
- wayland-server, wayland-client, wayland-egl and `wayland-scanner`
- libdrm, gbm, EGL/GLESv2
- libinput, libudev, libseat — input and the tty on the DRM path
- xkbcommon
- lodepng (`liblodepng.a`), and the header-only cglm and cgltf
- [pway](https://github.com/oscar0pavon/pway) — the Wayland client library that
  supplies the window. A separate repo, installed to `/usr/local/lib/libpway.a`
  and `/usr/local/include/pway`

There is **no X11**.

The xdg-shell and linux-dmabuf protocol code is generated with
`wayland-scanner` and checked into git, so it does not have to be regenerated
to build. To regenerate it, from inside `source_code/compositor`:

```
./generate_wayland_protocol_files.sh
```

# To test

The repo path is hardcoded in the build, so it has to live at `/root/swordfish`
(and pway at `/root/pway`).

    make
    sudo make install

Run it with no arguments. On the DRM path it drives every connected display;
nested in another compositor it opens one window.

## Keys

Shortcuts go through the compositor itself rather than a modifier, so
everything else is passed on to the focused client.

| key | |
|---|---|
| `y` | open a terminal |
| `m` | open firefox |
| `j` / `k` | cycle focus to the next / previous window |
| `c` | close the focused window |
| `q` | quit swordfish |
| `w` | switch virtual terminal (DRM only) |
