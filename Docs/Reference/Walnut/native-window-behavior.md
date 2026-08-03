# Native Window Behavior Notes

## Required Win32 message order

For a custom frame, the future ZDE Win32 backend must resolve hit tests in this
order:

1. Return native edge or corner resize codes while the window is restored.
2. Return native window-control codes for custom minimize, maximize, and close
   controls when those controls are implemented.
3. Ask the titlebar View whether the point belongs to its drag region.
4. Return `HTCAPTION` only for an approved drag point.
5. Return `HTCLIENT` for all other interactive content.

The drag callback must use window-client coordinates. The UI View owns the
interactive rectangles; the platform backend owns Win32 messages and native
result codes.

## Pending manual matrix

- Windows 10 and Windows 11
- restored and maximized states
- every resize edge and corner
- double-click maximize/restore
- Snap Layouts and edge Snap
- 100%, 125%, 150%, and 200% DPI
- dual monitors with matching and mixed DPI
- menu, command search, and tab interaction inside the titlebar
