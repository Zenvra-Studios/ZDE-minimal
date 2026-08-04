# Zenvra Platform PRD

## Module: Custom Window Framework (Windows)

Version: 0.1.0\
Status: Draft

## Vision & Mission

**Vision:** Become the foundation of a development platform that unifies
UI, multiple graphics engines, and an audio engine in one clean, fast,
cross-platform abstraction (Windows, Linux, macOS).

**Mission:**

-   Make the Custom Window Framework the hub that bridges multiple
    graphics engines (e.g., Skia for 2D/UI + a 3D engine) and an audio
    engine through one consistent API.
-   Keep the architecture modular & cross-platform so new backends
    (Linux, macOS, renderers, audio) can be added without touching the
    public API.
-   Prioritize high performance, zero native controls, and theme-ready
    support from the very first version.

## 1. Objective

Membangun fondasi **Custom Window Framework** untuk ekosistem Zenvra
dengan **Win32** sebagai backend pertama. Framework bertanggung jawab
atas lifecycle window, custom window chrome, input dasar, DPI awareness,
serta integrasi renderer (Skia).

Target jangka panjang: - Windows (Win32) - Linux (Wayland/X11) - macOS
(Cocoa/Objective-C++ atau Swift interop)

## 2. Goals

### Functional

-   Native Window Creation
-   Borderless Window
-   Custom Title Bar
-   Custom Caption Buttons
-   Native Drag & Resize
-   Snap Layout Compatibility
-   High DPI
-   Multi Window
-   Renderer Integration (Skia)

### Non Functional

-   Cross Platform Architecture
-   Zero Native Win32 Controls
-   Modular Design
-   High Performance
-   Theme Ready

## 3. Out of Scope (v0.1)

-   Docking
-   Widget Toolkit
-   Text Editor
-   IDE
-   Property Grid
-   Animation

## 4. High Level Architecture

``` text
Application
    │
Window Manager
    │
Platform Abstraction
    │
Windows Backend (Win32)
    │
Graphics Backend
    │
Skia
```

## 5. Project Structure

``` text
ZenWindow/
├── Include/
├── Source/
├── Platform/
│   ├── Windows/
│   ├── Linux/
│   └── macOS/
├── Graphics/
├── Input/
├── Common/
└── CMakeLists.txt
```

## 6. Windows Module

``` text
Platform/Windows/
└── Window/
    ├── Window.h
    ├── Window.cpp
    ├── WindowCreate.cpp
    ├── WindowDestroy.cpp
    ├── WindowMessage.cpp
    ├── WindowHitTest.cpp
    ├── WindowResize.cpp
    ├── WindowState.cpp
    ├── WindowChrome.cpp
    ├── WindowStyle.cpp
    ├── WindowDPI.cpp
    ├── WindowFullscreen.cpp
    └── WindowCursor.cpp
```

## 7. Responsibilities

### Window

-   HWND lifecycle
-   Message dispatch
-   Renderer binding

### WindowChrome

-   Title bar
-   Menu region
-   Caption buttons

### WindowHitTest

-   Drag region
-   Resize region
-   Border detection

### WindowState

-   Minimize
-   Restore
-   Maximize
-   Fullscreen

### WindowStyle

-   Borderless
-   Rounded corner
-   Future: Acrylic / Mica

## 8. Input

Mouse: - Move - Down - Up - Double Click - Wheel

Keyboard: - Key Down - Key Up - Character - IME

## 9. DPI

Support: - 100% - 125% - 150% - 175% - 200% - 250% - 300%

Message: - WM_DPICHANGED

## 10. Window Events

-   OnCreate
-   OnDestroy
-   OnResize
-   OnMove
-   OnFocus
-   OnLostFocus
-   OnClose

## 11. Public API

``` cpp
Window window;

window.Create();
window.Show();
window.SetTitle("Zenvra Development Studio");
window.Maximize();
window.Restore();
window.Close();
```

## 12. Roadmap

### Milestone 1

-   Native Win32 Window
-   Message Loop
-   DPI Awareness

### Milestone 2

-   Custom Chrome
-   Custom Caption Buttons
-   Drag
-   Resize
-   Snap Layout

### Milestone 3

-   Skia Integration
-   Render Loop

### Milestone 4

-   Input Abstraction
-   Clipboard
-   Cursor
-   IME

### Milestone 5

-   Platform Abstraction
-   Linux Backend
-   macOS Backend
