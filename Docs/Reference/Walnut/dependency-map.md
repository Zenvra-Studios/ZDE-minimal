# Walnut Reference Dependency Map

```text
Walnut ApplicationGUI
    |-- forked GLFW
    |     |-- GLFW_TITLEBAR
    |     `-- glfwSetTitlebarHitTestCallback
    |-- Dear ImGui docking
    |-- Vulkan backend
    `-- Win32 non-client behavior through GLFW

ZDE target ownership
    |-- Application (composition and lifecycle)
    |-- Platform/IPlatformWindow
    |     `-- Platform/Win32 (native behavior)
    |-- Rendering (future Vulkan ownership)
    |-- UI/Chrome (future titlebar View)
    |-- Commands (Model)
    `-- Application/ViewModels (UI actions and state)
```

No public ZDE header includes Walnut or its GLFW fork. If a temporary adapter
is ever required, it must remain private to the platform backend and must not
become part of the Studio contract.
