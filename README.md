# Zenvra Development Studio (ZDE)

A development platform for building creative tools / applications that
unify UI, graphics, and audio in a single ecosystem.

## Vision

To become the foundation of a development platform that unifies UI,
multiple graphics engines, and an audio engine behind one clean, fast,
cross-platform abstraction (Windows, Linux, macOS).

## Mission

- Build a **custom window framework** (Win32 as the first backend) as
  the hub bridging multiple graphics engines (e.g., Skia for 2D/UI +
  a 3D engine) and an audio engine.
- Keep the architecture modular & cross-platform so any backend can be
  added without changing the public API.
- Prioritize quality: high performance, zero native controls, theme-ready.

## Project Structure

```
ZenvraDevelopmentStudio/
├── Source/         # Application code (UI, Platform, Services, etc.)
├── Drivers/        # Driver / backend layer
├── ThirdParty/     # Third-party glue
├── Docs/           # Documentation & PRDs
├── Scripts/        # Build & utility scripts
├── Config/         # Configuration
├── Tests/          # Unit tests
├── Cmake/          # CMake helpers (CPM.cmake, toolchains)
├── CMakeLists.txt
└── CMakePresets.json
```

## Prerequisites

- **CMake** >= 3.25
- A C++20 compiler, any of:
  - Visual Studio 2022 (MSVC) — for the `vs2022` presets
  - GCC/MinGW + Ninja — for the `ninja` presets
  - clang-cl + Ninja — for the `clang-ninja` presets (good for clangd)
- Internet connection on first configure (dependencies are fetched
  automatically via CPM.cmake).

## Setup & Build (Windows — quick & simple)

Simplest path: use the Visual Studio 2022 preset (Debug).

```powershell
cmake --preset windows-x64-vs2022-debug
cmake --build --preset windows-x64-vs2022-debug
```

Or with Ninja + MinGW:

```powershell
cmake --preset windows-x64-ninja-debug
cmake --build --preset windows-x64-ninja-debug
```

Build output lives outside the source tree:

```
build/<preset>/bin/<config>/    # executables (.exe) & runtime DLLs
build/<preset>/lib/<config>/    # static / import libraries
```

## Setup & Build (Linux / macOS)

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
```

For creating Linux packages (.deb, .rpm, .pkg.tar.zst) and managing installation/uninstallation on Linux, see the detailed guide:
- [Linux Packaging, Installation & Uninstall Guide](Docs/LINUX_PACKAGING_AND_INSTALLATION.md)

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

## Temporary Setup Notes

- First configure requires network access — CPM.cmake downloads
  dependencies (e.g., Skia tooling) into `build/_deps`.
- For IDEs: open this folder directly in Visual Studio / CLion, or
  generate `compile_commands.json` via the `windows-x64-clang-ninja-debug`
  preset (`EXPORT_COMPILE_COMMANDS=ON` is already enabled in all presets).
- Full preset list (Debug/Release, VS/Ninja/clang, MinGW cross-compile
  from Linux) is in `CMakePresets.json`.
