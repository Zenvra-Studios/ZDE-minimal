# Main Toolbar System Planning & Guidelines

Dokumen perencanaan dan arsitektur lengkap untuk **ZDE Studio Main Toolbar** (Build, Run, Debug, Binary Targets, Arch, Release/Debug Modes) tersedia di:
- [Docs/Architecture/main-toolbar.md](file:///Users/ahmadzanisy/Desktop/Project/ZDE-minimal/Docs/Architecture/main-toolbar.md)

---

## 1. Fitur Utama Toolbar

```
+-------------------------------------------------------------------------------------------------------------------------------+
| [Left Section]           |                              [Center Section - Main Execution Controls]             | [Right Section] |
| Workspace / Branch       |  Target Selector Combo Box                 | Action Buttons                         | Search & Layout |
| [ 📁 ZDE | 🌿 main ▾ ]   |  [ ⚙️ ZDE | Debug | arm64 (macos-debug) ▾ ] | [ ▶ Run ] [ 🐞 Debug ] [ 🔨 Build ] [ ⏹ ] | [ 🔍 Search ]   |
+-------------------------------------------------------------------------------------------------------------------------------+
```

1. **Tombol Aksi**:
   - **Play / Run (`▶`)**: Menjalankan binary yang terpilih.
   - **Debug (`🐞`)**: Menjalankan sesi debugging (LLDB/GDB) pada binary.
   - **Build (`🔨`)**: Melakukan kompilasi CMake target pada preset aktif.
   - **Stop (`⏹`)**: Menghentikan proses build atau aplikasi yang sedang aktif.
2. **Popup Pemilih Konfigurasi (Target Selector Dropdown)**:
   - **Binary Target**: Memilih executable yang ingin dijalankan/dibuild (`ZDE`, `ZDEUnitTests`, custom targets).
   - **Mode Konfigurasi**: `Debug` vs `Release` vs `RelWithDebInfo` vs `MinSizeRel`.
   - **Arsitektur (Arch)**: `arm64` (Apple Silicon / AArch64), `x86_64` (Intel), atau `Universal`.
   - **CMake Preset**: `macos-debug`, `macos-release`, `linux-debug`, `windows-debug`, dsb.
   - **Edit Configurations**: Opsi membuka pengaturan argumen CLI dan environment variables.

---

## 2. Standar Penamaan (*Brand Neutrality*)

Dilarang keras menyematkan nama merek pihak ketiga (*JetBrains, CLion, IntelliJ, VSCode*, dll.) pada kode C++.
Gunakan penamaan domain native ZDE:
- Namespace: `Zenvra::UI::Toolbar`, `Zenvra::UI::Widgets`
- Classes: `StudioMainToolbar`, `RunConfigurationWidget`, `ActionButtonGroup`, `ProjectBranchWidget`
- Enums: `BuildConfigurationMode`, `TargetArchitecture`, `ExecutionState`, `ToolbarActionType`

---

## 3. Struktur Folder

```
Source/
├── Tools/                               -> Perkakas Eksekusi (Builder, Runner, Debugger)
│   ├── Builder/CMakeBuilder.h / .cpp    -> CMake --build & target discovery
│   ├── Runner/ProcessRunner.h / .cpp    -> Peluncur proses binary lintas platform
│   └── Debugger/DebuggerEngine.h / .cpp -> Tooling mini debugger (LLDB, GDB, DAP)
├── Services/                            -> Layer Koordinator Layanan
│   ├── Build/BuildService.h / .cpp      -> Asynchronous build runner & log streamer
│   └── Execution/ExecutionService.h     -> Lifecycle manager binary runner
├── UI/
│   └── Toolbar/
│       ├── StudioMainToolbar.h / .cpp
│       ├── ToolbarLayout.h / .cpp
│       ├── ToolbarTypes.h
│       └── Widgets/
│           ├── RunConfigurationWidget.h / .cpp
│           ├── ActionButtonGroup.h / .cpp
│           ├── ProjectBranchWidget.h / .cpp
│           └── QuickSearchWidget.h / .cpp
├── Application/
│   └── ViewModels/
│       └── MainToolbarViewModel.h / .cpp
└── Platform/
    ├── Cocoa/Components/CocoaChromeRenderer.h / .mm
    ├── Win32/Win32Window.cpp
    └── X11/X11Window.cpp
```
