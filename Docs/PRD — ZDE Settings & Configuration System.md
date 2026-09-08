# PRD — ZDE Settings & Configuration System

**Project:** ZDE-minimal  
**Feature:** Settings & Configuration System  
**Target:** Native C++ IDE  
**Reference UX:** Visual Studio Code / JetBrains IDE  
**Primary Goal:** Membuat sistem Settings native C++ yang terintegrasi dengan UI, Theme, Window, Layout, Editor, Shader Preview, Browser, Plugin, dan Workspace.

---

# 1. Overview

ZDE-minimal merupakan IDE native C++ yang memiliki arsitektur modular untuk editor, UI, workspace, plugins, terminal, source control, shader preview, dan berbagai subsystem lainnya.

Feature ini bertujuan membangun **Settings System terpusat** yang memungkinkan pengguna mengubah konfigurasi IDE secara runtime dan persistent.

UX Settings harus terinspirasi dari:

- Visual Studio Code Settings
- JetBrains Settings / Preferences
- Native desktop IDE conventions

Namun implementasinya harus tetap mengikuti arsitektur native C++ ZDE-minimal dan **tidak mengadopsi arsitektur Electron/TypeScript milik VS Code**.

---

# 2. Goals

## Primary Goals

Implementasi harus memungkinkan:

1. Membuka Settings melalui command.
2. Settings tampil sebagai floating overlay/window di atas IDE.
3. Settings dapat dicari.
4. Settings memiliki kategori.
5. Settings memiliki subcategory.
6. Setting dapat diubah secara runtime.
7. Perubahan setting langsung diterapkan tanpa restart jika memungkinkan.
8. Setting tersimpan secara persistent.
9. Setting memiliki default value.
10. Workspace dapat memiliki setting sendiri.
11. Plugin dapat mendaftarkan setting baru.
12. UI component dapat menggunakan setting tanpa mengetahui storage implementation.
13. Theme dapat dikontrol melalui Settings.
14. Font family dan font size dapat dikontrol melalui Settings.
15. Window/layout dapat dikontrol melalui Settings.
16. Editor dapat dikontrol melalui Settings.
17. Shader preview dapat dikontrol melalui Settings.
18. Browser integration nantinya dapat mendaftarkan setting.
19. Sistem dapat dikembangkan tanpa mengubah Settings UI secara manual setiap kali setting baru ditambahkan.

---

# 3. Non-Goals

Jangan implementasikan hal berikut pada tahap awal:

- Replikasi penuh source code VS Code.
- Electron.
- WebView sebagai UI utama Settings.
- TypeScript settings engine.
- Settings database kompleks.
- Cloud synchronization.
- Account synchronization.
- Remote settings synchronization.
- AI-generated settings.
- Marketplace settings UI.

Feature tersebut dapat dipertimbangkan pada tahap berikutnya.

---

# 4. Design Principles

## 4.1 Native First

Seluruh Settings System harus menggunakan C++ dan UI framework milik ZDE.

Jangan membuat Settings sebagai webpage.

## 4.2 Separation of Concerns

Pisahkan:

```text
Settings Data
Settings Logic
Settings UI
Subsystem Integration
```

Jangan mencampurkan semuanya dalam satu class.

## 4.3 Settings UI Tidak Boleh Mengelola Storage

Contoh yang TIDAK diinginkan:

```cpp
settingsWindow.saveToJson(...);
```

Settings UI hanya berinteraksi dengan:

```cpp
SettingsService
```

Contoh:

```cpp
settings.set("editor.fontSize", 16);
```

## 4.4 Schema Driven

Settings UI harus dapat dibuat berdasarkan schema/definition.

Contoh:

```text
editor.fontSize
```

memiliki:

```text
id
title
description
type
default value
range
category
subcategory
```

UI kemudian menentukan component berdasarkan type.

---

# 5. Proposed Architecture

Arsitektur utama:

```text
                    ┌────────────────────┐
                    │    Settings UI     │
                    └─────────┬──────────┘
                              │
                              ▼
                    ┌────────────────────┐
                    │ Settings Service   │
                    └─────────┬──────────┘
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
      Settings Store     Schema Registry   Event System
             │
             ▼
       User / Workspace
          Storage
```

Subsystem:

```text
SettingsService
      │
      ├── ThemeService
      ├── EditorService
      ├── WindowService
      ├── LayoutService
      ├── TerminalService
      ├── ShaderService
      ├── BrowserService
      └── PluginService
```

---

# 6. Directory Structure

Tambahkan subsystem Settings:

```text
Source/
├── Settings/
│   ├── SettingsService.h
│   ├── SettingsService.cpp
│   ├── SettingsStore.h
│   ├── SettingsStore.cpp
│   ├── SettingsSchema.h
│   ├── SettingsSchema.cpp
│   ├── SettingsDefinition.h
│   ├── SettingsDefinition.cpp
│   ├── SettingsValue.h
│   ├── SettingsEvent.h
│   ├── SettingsEvent.cpp
│   └── CMakeLists.txt
│
├── UI/
│   ├── Components/
│   │   ├── SettingCheckbox.*
│   │   ├── SettingDropdown.*
│   │   ├── SettingSlider.*
│   │   ├── SettingNumberInput.*
│   │   ├── SettingTextInput.*
│   │   ├── SettingColorPicker.*
│   │   └── SettingFontPicker.*
│   │
│   ├── Overlay/
│   │   ├── OverlayManager.*
│   │   ├── OverlayLayer.*
│   │   └── OverlayWindow.*
│   │
│   └── Settings/
│       ├── SettingsWindow.*
│       ├── SettingsSidebar.*
│       ├── SettingsSearch.*
│       ├── SettingsPage.*
│       ├── SettingsSection.*
│       ├── SettingsRenderer.*
│       └── SettingsController.*
│
└── ...
```

**Catatan:** Struktur di atas adalah target architecture. Sebelum membuat file baru, AI agent wajib memeriksa apakah repository sudah memiliki abstraction yang setara dan menggunakannya atau melakukan extension daripada membuat duplicate system.

---

# 7. Responsibilities

## 7.1 `Source/Settings`

Berisi logic dan data Settings.

Tidak boleh bergantung kepada UI.

Contoh:

```text
SettingsService
SettingsStore
SettingsSchema
SettingsDefinition
SettingsValue
```

## 7.2 `Source/UI/Settings`

Berisi presentation layer Settings.

Tanggung jawab:

- Sidebar
- Search
- Category navigation
- Setting rows
- Sections
- Rendering
- User interaction

Tidak bertanggung jawab terhadap persistence.

## 7.3 `Source/UI/Components`

Berisi reusable controls.

Contoh:

```text
SettingCheckbox
SettingDropdown
SettingSlider
SettingNumberInput
SettingTextInput
SettingColorPicker
SettingFontPicker
```

Component harus reusable di luar Settings jika diperlukan.

---

# 8. Settings Value System

Buat generic value container.

Minimal support:

```text
bool
integer
floating point
string
enum
color
font
array
object
```

Contoh konsep:

```cpp
using SettingsValue = std::variant<
    bool,
    int64_t,
    double,
    std::string,
    Color,
    FontDescriptor
>;
```

Implementasi boleh berbeda mengikuti utility/type system ZDE yang sudah tersedia.

Jangan membuat duplicate type jika ZDE sudah memiliki type abstraction yang sesuai.

---

# 9. Setting Definition

Setiap setting harus mempunyai metadata.

Contoh:

```cpp
struct SettingDefinition
{
    std::string id;
    std::string title;
    std::string description;

    SettingType type;

    SettingsValue defaultValue;

    std::string category;
    std::string subcategory;

    std::vector<std::string> tags;
};
```

Optional metadata:

```text
minimum
maximum
step
enum values
restart required
experimental
deprecated
```

---

# 10. Setting IDs

Gunakan namespace-style IDs.

Contoh:

```text
editor.fontSize
editor.fontFamily
editor.lineHeight

editor.minimap.enabled
editor.wordWrap
editor.cursorStyle

ui.fontSize
ui.fontFamily
ui.scale

theme.current

window.zoomLevel
window.fullscreen

workbench.sidebar.position
workbench.panel.position
workbench.activityBar.visible

terminal.fontSize
terminal.fontFamily

shader.preview.enabled
shader.preview.backend
shader.preview.fpsLimit

browser.defaultZoom
browser.devtools.enabled
```

Jangan menggunakan ID global seperti:

```text
fontSize
font
theme
sidebar
```

karena ID global akan mudah mengalami collision.

---

# 11. Settings Categories

Initial categories:

```text
General
Editor
Workbench
Window
Appearance
Theme
Text Editor
Terminal
Files
Workspace
Language
Source Control
Shader
Browser
Plugins
Debug
```

Tidak semua kategori harus langsung memiliki setting.

Implementasikan hanya kategori yang relevan dengan subsystem yang sudah tersedia.

---

# 12. Initial Settings

## 12.1 Editor

Implementasikan minimal:

```text
editor.fontSize
editor.fontFamily
editor.lineHeight
editor.wordWrap
editor.minimap.enabled
editor.cursorStyle
```

## 12.2 UI

```text
ui.fontSize
ui.fontFamily
ui.scale
```

## 12.3 Theme

```text
theme.current
```

Contoh:

```text
Dark
Light
High Contrast
```

Jika theme engine saat ini memiliki mekanisme berbeda, gunakan abstraction yang sudah ada.

## 12.4 Window

```text
window.zoomLevel
window.fullscreen
window.titleBarStyle
```

## 12.5 Workbench

```text
workbench.sidebar.position
workbench.panel.position
workbench.activityBar.visible
```

## 12.6 Terminal

```text
terminal.fontSize
terminal.fontFamily
```

## 12.7 Shader

```text
shader.preview.enabled
shader.preview.backend
shader.preview.fpsLimit
shader.preview.vsync
shader.preview.autoReload
```

Shader settings harus dipisahkan dari editor settings.

## 12.8 Browser

Initial settings:

```text
browser.defaultZoom
browser.devtools.enabled
```

Browser settings hanya perlu disiapkan untuk extensibility jika browser subsystem belum selesai.

---

# 13. Settings Storage

Settings harus persistent.

Gunakan format yang sederhana dan mudah di-debug.

Preferred:

```text
JSON
```

Contoh:

```json
{
    "editor.fontSize": 16,
    "editor.fontFamily": "Cascadia Code",
    "editor.minimap.enabled": true,
    "theme.current": "Dark",
    "window.zoomLevel": 0
}
```

Lokasi storage harus mengikuti platform abstraction milik ZDE.

Jangan hard-code path seperti:

```text
C:\Users\...
/home/...
~/Library/...
```

di SettingsService.

Gunakan Platform/FileSystem service yang sudah tersedia.

---

# 14. Settings Scope

Settings minimal memiliki:

```text
Default
User
Workspace
```

Hierarchy:

```text
Default
   ↓
User
   ↓
Workspace
```

Contoh:

```text
Default:
editor.fontSize = 14

User:
editor.fontSize = 16

Workspace:
editor.fontSize = 18
```

Effective value:

```text
18
```

Workspace override harus mengalahkan User setting.

---

# 15. Settings Store

Buat abstraction:

```cpp
class SettingsStore
{
public:
    virtual ~SettingsStore() = default;

    virtual bool load() = 0;
    virtual bool save() = 0;

    virtual bool contains(std::string_view key) const = 0;

    virtual SettingsValue get(
        std::string_view key
    ) const = 0;

    virtual void set(
        std::string_view key,
        SettingsValue value
    ) = 0;

    virtual void remove(
        std::string_view key
    ) = 0;
};
```

Implementasi storage dapat berupa:

```text
JsonSettingsStore
```

Namun gunakan API filesystem/parser yang sudah dimiliki project jika tersedia.

---

# 16. Settings Service

`SettingsService` menjadi API utama seluruh aplikasi.

Contoh:

```cpp
class SettingsService
{
public:

    SettingsValue get(
        std::string_view id
    ) const;

    template<typename T>
    T get(
        std::string_view id
    ) const;

    void set(
        std::string_view id,
        SettingsValue value
    );

    bool has(
        std::string_view id
    ) const;

    void reset(
        std::string_view id
    );

    void resetCategory(
        std::string_view category
    );

    void registerSetting(
        SettingDefinition definition
    );
};
```

Subsystem lain hanya perlu berkomunikasi dengan service ini.

---

# 17. Settings Events

Perubahan setting harus menghasilkan event.

Contoh:

```cpp
struct SettingsChangedEvent
{
    std::string id;

    SettingsValue oldValue;
    SettingsValue newValue;
};
```

Flow:

```text
settings.set()
      │
      ▼
SettingsChangedEvent
      │
      ├── Theme
      ├── Editor
      ├── Window
      ├── Layout
      └── Plugin
```

Jika ZDE sudah mempunyai event bus, gunakan event system tersebut.

Jangan membuat event bus kedua hanya untuk Settings.

---

# 18. Runtime Update

Setting yang tidak membutuhkan restart harus langsung diterapkan.

Contoh:

```text
editor.fontSize
```

User mengubah:

```text
14 → 18
```

Expected:

```text
SettingsService
      ↓
SettingsChangedEvent
      ↓
Editor
      ↓
Font updated
```

Tidak boleh memerlukan restart kecuali setting memang membutuhkan restart.

---

# 19. Settings UI

Settings harus berupa floating overlay.

Expected behavior:

```text
Main IDE
┌─────────────────────────────────────────┐
│                                         │
│             IDE Workspace               │
│                                         │
│       ┌─────────────────────────┐       │
│       │ Settings             X  │       │
│       ├──────────┬──────────────┤       │
│       │ General  │ Editor       │       │
│       │ Editor   │              │       │
│       │ Workbench│ Font Size    │       │
│       │ Theme    │ [ 16       ] │       │
│       │ Terminal │              │       │
│       │ Shader   │ Font Family  │       │
│       │ Browser  │ [Cascadia ▼] │       │
│       └──────────┴──────────────┘       │
│                                         │
└─────────────────────────────────────────┘
```

---

# 20. Overlay Architecture

Settings tidak boleh menjadi special-case window.

Buat atau extend generic:

```text
OverlayManager
```

yang nantinya dapat digunakan oleh:

```text
Settings
Command Palette
Quick Open
Search
Completion
Tooltip
Context Menu
Color Picker
Dialogs
```

Expected API:

```cpp
overlayManager.open(settingsWindow);
```

atau API equivalent sesuai arsitektur UI ZDE.

---

# 21. Overlay Requirements

Overlay harus mendukung:

```text
open
close
focus
bringToFront
resize
move
modal/non-modal
keyboard focus
mouse interaction
```

Settings harus:

```text
non-modal
focusable
resizable
closable
```

Jika architecture UI mendukung movable floating window, Settings juga harus dapat dipindahkan.

---

# 22. Settings Layout

Settings window minimal terdiri dari:

```text
SettingsWindow
│
├── Header
│   ├── Title
│   └── Close Button
│
├── Search
│
├── Sidebar
│   └── Categories
│
└── Content
    ├── Page
    ├── Section
    └── Setting Rows
```

---

# 23. Search

Search harus mencari:

```text
setting ID
title
description
tags
category
```

Contoh:

User mengetik:

```text
font
```

Result:

```text
Editor: Font Size
Editor: Font Family
Terminal: Font Size
UI: Font Size
```

Search harus mengubah content view tanpa restart Settings window.

---

# 24. Setting Row

Setiap setting ditampilkan sebagai:

```text
┌─────────────────────────────────────────────┐
│ Font Size                         [ 16 ]    │
│ Controls the font size of the editor.       │
└─────────────────────────────────────────────┘
```

Boolean:

```text
┌─────────────────────────────────────────────┐
│ Enable Minimap                     [ ✓ ]    │
│ Shows the editor minimap.                   │
└─────────────────────────────────────────────┘
```

Dropdown:

```text
Font Family                      [Cascadia ▼]
```

Slider:

```text
UI Scale                         ─────●────
```

---

# 25. UI Components

Minimal implement:

```text
SettingCheckbox
SettingDropdown
SettingSlider
SettingNumberInput
SettingTextInput
```

Optional:

```text
SettingColorPicker
SettingFontPicker
SettingKeybinding
SettingFilePicker
```

Jangan membuat component jika component equivalent sudah ada di:

```text
Source/UI/Components
```

Gunakan existing component terlebih dahulu.

---

# 26. Font System

Settings harus mendukung:

```text
editor.fontFamily
editor.fontSize
editor.lineHeight

ui.fontFamily
ui.fontSize
```

Font handling harus melalui FontManager / Theme / rendering abstraction yang sesuai.

Jangan membuat font loading langsung dari SettingsWindow.

Flow:

```text
Settings
 ↓
SettingsService
 ↓
FontService / FontManager
 ↓
Renderer
```

---

# 27. Theme System

Settings harus dapat mengganti theme.

Flow:

```text
theme.current
      ↓
SettingsService
      ↓
ThemeService
      ↓
UI
```

Semua UI component harus mendapatkan token/theme dari theme system.

Jangan hard-code color Settings UI.

---

# 28. Layout Settings

Settings harus dapat mengontrol layout.

Contoh:

```text
workbench.sidebar.position
workbench.panel.position
workbench.activityBar.visible
```

Flow:

```text
SettingsService
      ↓
LayoutService
      ↓
Workbench
```

Settings UI tidak boleh mengubah layout secara langsung.

---

# 29. Plugin Integration

Plugin API harus dapat mendaftarkan setting.

Contoh:

```cpp
plugin.settings().registerSetting({
    .id = "myPlugin.enabled",
    .title = "Enable Feature",
    .description = "Enable my plugin feature.",
    .type = SettingType::Boolean,
    .defaultValue = true
});
```

Kemudian Settings UI otomatis menampilkan setting tersebut.

Plugin tidak perlu memodifikasi:

```text
SettingsWindow.cpp
```

---

# 30. Plugin Settings Namespace

Plugin harus menggunakan namespace.

Valid:

```text
myPlugin.enabled
myPlugin.theme
myPlugin.server.port
```

Tidak valid:

```text
enabled
theme
port
```

Jika plugin ID adalah:

```text
shaderTools
```

maka:

```text
shaderTools.preview.enabled
shaderTools.preview.fps
```

---

# 31. Shader Settings

Karena ZDE akan memiliki Shader Preview/Sandbox, Settings System harus disiapkan untuk:

```text
shader.preview.enabled
shader.preview.backend
shader.preview.fpsLimit
shader.preview.vsync
shader.preview.autoReload
```

Perubahan:

```text
shader.preview.fpsLimit
```

harus dapat diterapkan ke shader preview runtime jika memungkinkan.

---

# 32. Browser Settings

Browser integration nantinya membutuhkan:

```text
browser.defaultZoom
browser.devtools.enabled
browser.javascript.enabled
browser.homepage
```

Browser plugin/subsystem harus membaca setting melalui:

```cpp
settings.get("browser.defaultZoom");
```

Jangan menyimpan setting browser di browser UI sendiri.

---

# 33. Command Integration

Tambahkan command:

```text
Open Settings
```

Command harus membuka:

```text
Command
  ↓
OverlayManager
  ↓
SettingsWindow
```

Jangan membuat Settings Window langsung dari key event.

---

# 34. Settings Reset

Support:

```text
Reset Setting
Reset Category
Reset All Settings
```

Contoh:

```text
editor.fontSize
```

di-reset menjadi:

```text
defaultValue
```

---

# 35. Validation

Setting harus divalidasi sebelum disimpan.

Contoh:

```text
editor.fontSize
minimum = 8
maximum = 72
```

Input:

```text
100
```

harus ditolak atau di-clamp sesuai policy yang ditentukan.

---

# 36. Error Handling

Jika settings file:

- tidak ditemukan
- corrupt
- invalid
- permission denied

IDE tidak boleh crash.

Expected behavior:

```text
Invalid Settings
      ↓
Log error
      ↓
Fallback ke defaults
      ↓
Continue launching IDE
```

Jika memungkinkan, backup settings lama sebelum overwrite.

---

# 37. Performance

Settings access harus murah.

Contoh:

```cpp
settings.get("editor.fontSize");
```

tidak boleh membaca JSON file dari disk setiap kali dipanggil.

Expected:

```text
Disk
 ↓
SettingsStore
 ↓
Memory
 ↓
SettingsService
```

Disk hanya digunakan saat:

```text
load
save
```

---

# 38. Thread Safety

Settings System harus mengikuti threading model ZDE.

Jika subsystem UI berjalan pada UI thread, perubahan yang memengaruhi UI harus diproses melalui mekanisme UI dispatch yang sudah tersedia.

Jangan membuat thread baru hanya untuk Settings jika tidak diperlukan.

---

# 39. Dependency Rules

## Settings Core boleh bergantung pada:

```text
Core
Utility
Filesystem abstraction
Serialization abstraction
Event system
```

## Settings Core TIDAK boleh bergantung pada:

```text
UI
SettingsWindow
Editor UI
Theme UI
```

## UI Settings boleh bergantung pada:

```text
Settings
UI Components
Theme
Overlay
Core
```

## Editor boleh bergantung pada:

```text
SettingsService
```

tetapi:

```text
Editor
```

tidak boleh bergantung pada:

```text
SettingsWindow
```

---

# 40. Anti-Patterns

Jangan lakukan:

```cpp
SettingsWindow::save();
```

yang langsung menulis JSON.

Jangan membuat:

```text
SettingsWindow
 ├── Editor
 ├── Theme
 ├── Terminal
 ├── Browser
 └── Layout
```

Gunakan:

```text
SettingsWindow
      ↓
SettingsService
      ↓
Subsystem
```

Jangan membuat banyak abstraction yang belum diperlukan seperti:

```text
SettingsManager
SettingsController
SettingsService
SettingsStore
SettingsRepository
SettingsProvider
SettingsContext
```

semuanya sekaligus tanpa alasan.

Mulai dari abstraction yang benar-benar dibutuhkan.

---

# 41. Implementation Phases

## Phase 1 — Repository Inspection

Sebelum coding:

1. Inspect root directory.
2. Inspect `Source`.
3. Inspect `Source/UI`.
4. Inspect `Source/UI/Components`.
5. Inspect `Source/UI/Theme`.
6. Inspect `Commands`.
7. Inspect `Services`.
8. Inspect window/overlay/modal implementation.
9. Inspect event system.
10. Inspect filesystem abstraction.
11. Inspect serialization/JSON implementation.
12. Inspect CMake configuration.

Output dari phase ini harus berupa pemetaan architecture existing dan daftar abstraction yang dapat digunakan kembali.

**Jangan langsung membuat class baru sebelum phase ini selesai.**

---

## Phase 2 — Settings Core

Implement:

```text
SettingsValue
SettingsDefinition
SettingsSchema
SettingsStore
SettingsService
```

Target:

```cpp
settings.set("editor.fontSize", 16);

auto size =
    settings.get<int>("editor.fontSize");
```

---

## Phase 3 — Persistence

Implement:

```text
JSON storage
User settings
Workspace settings
Default settings
```

Target:

```text
IDE restart
↓
Settings tetap tersimpan
```

---

## Phase 4 — Events

Implement:

```text
SettingsChangedEvent
```

Target:

```text
settings.set()
↓
event
↓
subsystem update
```

Gunakan existing event system jika tersedia.

---

## Phase 5 — Overlay

Implement atau extend:

```text
OverlayManager
OverlayLayer
OverlayWindow
```

Jangan membuat duplicate jika existing modal/popup architecture sudah dapat diperluas.

---

## Phase 6 — Settings UI

Implement:

```text
SettingsWindow
SettingsSidebar
SettingsSearch
SettingsPage
SettingsSection
SettingsRenderer
SettingsController
```

---

## Phase 7 — UI Components

Implement:

```text
SettingCheckbox
SettingDropdown
SettingSlider
SettingNumberInput
SettingTextInput
```

Gunakan existing components sebanyak mungkin.

---

## Phase 8 — Editor Integration

Implement:

```text
editor.fontSize
editor.fontFamily
editor.lineHeight
editor.minimap.enabled
editor.wordWrap
```

Pastikan perubahan live.

---

## Phase 9 — Theme Integration

Implement:

```text
theme.current
ui.fontSize
ui.fontFamily
```

Pastikan seluruh UI menggunakan theme/token system.

---

## Phase 10 — Workbench Integration

Implement:

```text
workbench.sidebar.position
workbench.panel.position
workbench.activityBar.visible
window.zoomLevel
```

---

## Phase 11 — Shader Integration

Implement:

```text
shader.preview.enabled
shader.preview.backend
shader.preview.fpsLimit
shader.preview.vsync
shader.preview.autoReload
```

Jika shader subsystem belum siap, buat integration point tanpa mengimplementasikan shader engine penuh.

---

## Phase 12 — Plugin Integration

Expose:

```cpp
registerSetting()
```

melalui plugin API.

Plugin setting harus otomatis muncul pada Settings UI.

---

## Phase 13 — Browser Integration

Prepare namespace:

```text
browser.*
```

Browser subsystem dapat mengonsumsi setting melalui SettingsService.

Jangan mengimplementasikan browser engine hanya untuk menyelesaikan Settings System.

---

# 42. Acceptance Criteria

## Core

- [ ] SettingsService tersedia.
- [ ] Setting dapat di-register.
- [ ] Setting dapat di-read.
- [ ] Setting dapat diubah.
- [ ] Setting dapat di-reset.
- [ ] Default value bekerja.
- [ ] Setting validation bekerja.

## Persistence

- [ ] User settings tersimpan.
- [ ] Workspace settings tersimpan.
- [ ] Settings tetap ada setelah restart.
- [ ] Corrupt settings tidak menyebabkan crash.

## UI

- [ ] Settings dapat dibuka melalui command.
- [ ] Settings tampil sebagai overlay.
- [ ] Settings dapat ditutup.
- [ ] Settings dapat di-scroll.
- [ ] Category navigation bekerja.
- [ ] Search bekerja.
- [ ] Setting controls bekerja.

## Runtime

- [ ] Font size dapat berubah tanpa restart.
- [ ] Font family dapat berubah tanpa restart jika backend mendukung.
- [ ] Theme dapat berubah secara runtime.
- [ ] Layout dapat berubah secara runtime.
- [ ] Shader settings dapat berubah secara runtime jika subsystem mendukung.

## Extensibility

- [ ] Plugin dapat register setting.
- [ ] Plugin setting muncul otomatis pada Settings UI.
- [ ] Plugin tidak perlu memodifikasi Settings UI.
- [ ] Setting namespace mencegah collision.

---

# 43. Testing

Minimal test:

```text
SettingsService
├── register setting
├── get setting
├── set setting
├── reset setting
├── default value
├── validation
├── persistence
├── workspace override
└── change event
```

## Runtime Test

```text
1. Launch ZDE.
2. Open Settings.
3. Change editor.fontSize from 14 → 18.
4. Editor langsung berubah.
5. Close ZDE.
6. Launch ZDE.
7. editor.fontSize masih 18.
```

## Workspace Test

```text
User:
editor.fontSize = 16

Workspace:
editor.fontSize = 20

Effective:
20
```

## Plugin Test

```text
Plugin registers:

examplePlugin.enabled

↓

Settings UI automatically shows:

Example Plugin
└── Enabled
```

---

# 44. UX Requirements

Settings UX harus terasa familiar bagi pengguna VS Code/JetBrains.

Karakteristik:

```text
Fast
Clean
Searchable
Keyboard friendly
Non-blocking
Resizable
Theme aware
Consistent
```

Jangan membuat Settings terlihat seperti legacy Windows dialog.

Gunakan visual language ZDE sendiri yang konsisten dengan existing UI.

---

# 45. Keyboard Navigation

Settings harus mendukung:

```text
Tab
Shift + Tab
Arrow keys
Enter
Escape
```

Search harus dapat difokuskan melalui keyboard.

`Escape` harus menutup overlay jika tidak ada editing state yang sedang membutuhkan Escape.

---

# 46. Accessibility

Minimal:

- text readable
- sufficient contrast
- keyboard navigation
- focus indicator
- tooltip/description
- scalable font/UI size

---

# 47. Future Extensions

Architecture harus memungkinkan:

```text
Keybindings
Profiles
Settings Sync
Workspace Settings
Language-specific Settings
Plugin Settings
Experimental Settings
Settings Import/Export
Settings Schema Versioning
Settings Migration
```

Tanpa perlu redesign total SettingsService.

---

# 48. Important Instructions for AI Agent

Saat mengimplementasikan feature ini:

1. **Baca terlebih dahulu struktur repository ZDE-minimal.**
2. Identifikasi UI framework dan existing component system.
3. Identifikasi existing:
   - event system
   - filesystem abstraction
   - JSON/serialization
   - theme system
   - command system
   - window system
   - overlay/modal system
   - service locator/application service
4. Jangan membuat abstraction duplicate jika functionality tersebut sudah tersedia.
5. Jangan mengubah architecture besar-besaran tanpa alasan.
6. Jangan menghapus existing components.
7. Gunakan naming convention yang sudah digunakan repository.
8. Ikuti CMake structure yang sudah digunakan project.
9. Pastikan build existing tetap bekerja.
10. Implementasikan feature secara incremental.
11. Setelah setiap phase, lakukan build/test.
12. Jangan mengimplementasikan Browser/Plugin/Shader subsystem penuh hanya demi Settings.
13. Untuk subsystem yang belum tersedia, cukup buat integration point.
14. Settings UI tidak boleh mengetahui detail persistence.
15. Settings Core tidak boleh bergantung pada UI.
16. Gunakan existing Theme system untuk rendering Settings.
17. Gunakan existing UI Components sebelum membuat component baru.
18. Gunakan existing Overlay/Modal system jika sudah cukup.
19. Jika existing Overlay system belum cukup, extend menjadi generic OverlayManager.
20. Jangan menggunakan WebView untuk membuat Settings UI.
21. Jangan melakukan refactor besar terhadap subsystem yang tidak berkaitan.
22. Jangan mengubah public API existing kecuali memang diperlukan.
23. Setiap perubahan architecture harus memiliki alasan yang jelas.
24. Prioritaskan reuse terhadap implementation existing.
25. Setelah implementasi, lakukan build verification.

---

# 49. Expected Final Architecture

Target akhir:

```text
ZDE
│
├── Application
│
├── Core
│
├── Commands
│
├── Editors
│
├── Plugins
│
├── Services
│
├── Settings
│   ├── SettingsService
│   ├── SettingsStore
│   ├── SettingsSchema
│   ├── SettingsDefinition
│   └── SettingsValue
│
├── Workspace
│
└── UI
    │
    ├── Components
    │
    ├── Theme
    │
    ├── Overlay
    │
    ├── Settings
    │   ├── SettingsWindow
    │   ├── SettingsSidebar
    │   ├── SettingsSearch
    │   ├── SettingsPage
    │   ├── SettingsSection
    │   └── SettingsRenderer
    │
    ├── Editor
    └── Chrome
```

Dependency:

```text
                    ┌─────────────────┐
                    │  Settings UI   │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ SettingsService │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
          ThemeService   EditorService   LayoutService
              │              │              │
              ▼              ▼              ▼
             UI            Editor         Workbench


Plugin
   │
   └──── registerSetting()
              │
              ▼
       SettingsService
              │
              ▼
         Settings UI
```

---

# 50. Definition of Done

Feature dinyatakan selesai ketika ZDE memiliki Settings System native C++ yang:

```text
✓ Searchable
✓ Persistent
✓ Schema-driven
✓ Runtime configurable
✓ Workspace aware
✓ Theme aware
✓ Overlay based
✓ Plugin extensible
✓ Cross-platform ready
✓ Decoupled from UI
✓ Decoupled from storage
✓ Compatible dengan architecture ZDE
```

Pengguna harus dapat melakukan:

```text
Ctrl + ,
   ↓
Settings Overlay
   ↓
Editor
   ↓
Font Size
   ↓
18
   ↓
Editor langsung berubah
   ↓
Close
   ↓
Restart ZDE
   ↓
Font Size tetap 18
```

tanpa Settings UI perlu mengetahui bagaimana editor, filesystem, JSON storage, theme engine, atau plugin system bekerja secara internal.