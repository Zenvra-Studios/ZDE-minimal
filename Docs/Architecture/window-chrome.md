# Window Chrome Architecture

## Direction

Every top-level ZDE window should use one reusable custom-chrome system. A
window backend must not hand-code a different titlebar for the main window,
tool windows, dialogs, and future engine windows.

```text
Window specification
        |
        v
Window chrome ViewModel
  - title and workspace state
  - menu/command contributions
  - enabled and checked state
        |
        v
Window chrome layout + theme
  - DPI-scaled rectangles
  - responsive visibility
  - interactive exclusions
        |
        +-------------------+
        v                   v
Win32 chrome adapter    Future platform adapter
  - WM_NCCALCSIZE          - native equivalent
  - WM_NCHITTEST
  - native resize/Snap
  - GDI bootstrap View
```

## Window roles

The next contract extension should describe the role of each top-level
window instead of adding more booleans:

- `Main`: logo, full menu, command center, minimize/maximize/close.
- `Tool`: compact title, optional menu, minimize/maximize/close.
- `Dialog`: title and close, normally no maximize or menu.
- `Popup`: no custom chrome; transient UI-owned surface.

Most IDE panels should remain docked Views inside the main window. Native tool
windows should only be created when a panel is detached or when the operating
system requires a separate top-level surface.

## Ownership rules

- `WindowChromeLayout` computes rectangles and hit regions without Win32,
  Vulkan, or ImGui dependencies.
- `StudioTheme` owns all visual tokens.
- The platform adapter converts hit regions into native result codes.
- `StudioViewModel` owns command availability and execution.
- Views emit stable command IDs and never implement product actions directly.
- Renderer-specific drawing can replace the current GDI bootstrap without
  changing native resize, Snap, command, or layout contracts.

This keeps the visual chrome consistent across all ZDE windows while allowing
each platform to preserve its own native desktop behavior.
