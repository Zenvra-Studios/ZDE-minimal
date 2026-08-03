# Linux X11 Platform Backend

The Linux desktop backend lives entirely under `Source/Platform/X11` and
implements the shared `IPlatformWindow` contract. Application and ViewModel
code do not include Xlib headers.

## Responsibilities

- `Runtime/X11Context` owns the process-wide `Display`, initializes Xlib
  threading before opening it, installs error handlers, and reference-counts
  the connection for future multi-window use.
- `X11Window` owns the native window, ICCCM metadata, EWMH interactions,
  event routing, DPI scale, cursor feedback, custom-chrome hit testing, and
  command dispatch.
- `Components/X11ChromeRenderer` is the bootstrap View. It double-buffers the
  shared ZDE theme and chrome layout into an X11 pixmap and draws menus,
  command center, window controls, and popup command states without a native
  widget toolkit.
- `UI/Chrome/WindowMenuModel` is platform-neutral. Win32 and X11 bind the same
  stable command IDs to `StudioViewModel` through `CommandRegistry`.

## Window-manager integration

Custom chrome is requested through `_MOTIF_WM_HINTS`. Move and resize use
`_NET_WM_MOVERESIZE` when advertised by the window manager, preserving native
edge behavior and desktop snap. Maximize and restore use `_NET_WM_STATE` with
the horizontal and vertical maximize atoms. Minimize uses `XIconifyWindow`,
and close requests follow `WM_DELETE_WINDOW`.

When a minimal window manager does not advertise the EWMH operations, the
backend falls back to pointer-grab move/resize and restores from a maximized
work area obtained from `_NET_WORKAREA`. The capability report exposes whether
native snap is actually available.

## Input and scaling

Mouse input supports titlebar dragging, eight resize edges/corners,
double-click maximize, window buttons, menu switching, and popup commands.
Keyboard input supports Alt+F4, Alt plus each menu mnemonic, arrow navigation,
Enter, and Escape. Disabled and checked command states are queried from the
ViewModel at render time.

The initial scale is read from `Xft.dpi`, with physical screen DPI as a
fallback. Xlib exposes screen-wide DPI rather than dependable per-monitor DPI,
so `per_monitor_dpi` remains false until the renderer/window layer adopts an
XRandR monitor policy.

## Build and verification

On Debian or Ubuntu, install the native build prerequisites:

```sh
sudo apt-get install cmake ninja-build libx11-dev xauth xvfb
cmake --preset linux-release -DBUILD_TESTING=ON
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
xvfb-run --auto-servernum build/linux-release/bin/Release/ZDE --smoke-test
```

The `Linux X11` workflow runs the same build, unit tests, and lifecycle smoke
test in a virtual X server.
