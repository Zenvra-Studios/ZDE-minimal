# Walnut `dev` Reference Audit

## Scope

This audit records behavior observed in the local reference repository at
`C:/Users/Administrator/Documents/Projects/Walnut`. The repository was read
from branch `dev` and was not modified. Walnut remains a technical reference;
it is not a ZDE source or runtime dependency.

## Relevant code paths

- `Walnut/Platform/GUI/Walnut/ApplicationGUI.cpp`
- `Walnut/Platform/GUI/Walnut/ApplicationGUI.h`
- `vendor/GLFW/include/GLFW/glfw3.h`
- `vendor/GLFW/src/window.c`
- `vendor/GLFW/src/win32_window.c`

## Confirmed behavior

- `GLFW_TITLEBAR` is a fork-only window hint used to disable the native
  titlebar without discarding the thick resize frame.
- `glfwSetTitlebarHitTestCallback` passes the UI-computed drag state into the
  Win32 non-client hit test.
- Win32 processes resize edges and corners before testing the client-side drag
  region. This ordering is required so a drag region cannot consume resizing.
- `WM_NCCALCSIZE` contracts the client rectangle around the resize border when
  the custom titlebar is active.
- The titlebar UI excludes the menu from the drag region and provides explicit
  minimize, maximize/restore, and close controls.
- The root dockspace fills the main viewport and places the custom titlebar
  before the docking area.

## Adaptation decision

| Reference concern | ZDE decision |
|---|---|
| Fork-only GLFW titlebar API | Replace with ZDE-owned `IPlatformWindow`; keep backend details inside `Platform/Windows` |
| UI hover callback | Adapt as `TitlebarHitTestCallback` |
| Non-client Win32 handling | Reimplement in the Windows backend during the custom-chrome phase |
| Titlebar rendering | Reimplement as a dedicated ZDE View with separate layout, theme, and result types |
| Window commands in titlebar | Route through `StudioViewModel` and `CommandRegistry` |
| Root dockspace | Adapt into a dedicated dockspace View; do not place it in `Application` |
| Vulkan/ImGui lifecycle | Split into renderer-owned components before UI integration |

## Risks observed

- Walnut reads a global GLFW hint from Win32 window processing. ZDE must keep
  custom-chrome state per window.
- The reference titlebar uses fixed pixel metrics. ZDE must use DPI-scaled theme
  metrics.
- Titlebar, dockspace, render loop, Vulkan lifetime, and product UI are located
  in one implementation file. ZDE must keep these ownership boundaries split.
- The top resize edge has different visual behavior on Windows 10 and Windows
  11 and requires explicit validation on both systems.
- The callback only identifies a general caption region. ZDE must additionally
  exclude menus, search, tabs, and window buttons from dragging.

## Current validation status

- Source paths and fork-only APIs: confirmed.
- Non-client resize-before-caption algorithm: confirmed by source audit.
- Dockspace/titlebar composition: confirmed by source audit.
- Runtime drag, Snap, mixed-DPI, and multi-monitor behavior: pending manual test
  after the ZDE renderer and chrome View are available.
