# Platform Window Contract

`Platform/IPlatformWindow.h` is the only window contract visible to the
application layer. It exposes lifecycle, window state, native window actions,
capabilities, titlebar hit testing, and stable command notifications.

Backend-specific handles and APIs stay inside their platform folders. The
Windows backend supports custom non-client behavior and renders a GDI bootstrap
View from the shared ZDE chrome layout and theme. The Linux backend implements
the same contract directly with Xlib, EWMH, and Motif decoration hints. The
native titlebar remains available through `--native-titlebar` and safe UI mode.

The bootstrap Views are intentionally replaceable. A future ImGui/Vulkan View will use
the same layout rectangles, command IDs, capabilities, and Win32 hit-test
adapter instead of rewriting window behavior.

The local Walnut reference informed the future Win32 non-client algorithm, but
no Walnut or forked GLFW header is included by ZDE.
