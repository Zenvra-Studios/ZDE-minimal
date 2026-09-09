# ZDE Plugin & Marketplace
## Product Requirements Document (PRD)

**Status:** Draft  
**Version:** 0.1  
**Target:** ZDE  
**Platform:** Windows / Linux / macOS  
**Primary Architecture:** Native C++ IDE + Modular Plugin System + LSP

---

# 1. Overview

ZDE membutuhkan sistem extensibility yang memungkinkan fitur IDE dikembangkan dan didistribusikan secara modular.

Sistem ini harus mendukung dua jalur utama:

1. **Binary Installation**
   - Digunakan oleh pengguna umum.
   - Plugin diunduh dari Marketplace dalam bentuk prebuilt package.
   - Tidak membutuhkan proses compilation di mesin pengguna.

2. **Source Installation**
   - Digunakan oleh developer atau pengguna advanced.
   - Plugin dapat dipasang langsung dari Git repository.
   - ZDE melakukan clone, dependency detection, configure, build, package, dan installation secara otomatis.

Kedua jalur tersebut harus menghasilkan plugin yang masuk ke **Plugin Registry dan Runtime yang sama**.

```text
Marketplace
     │
     ▼
Binary Package
     │
     │
     ├──────────────┐
                    ▼
              Plugin Installer
                    │
Git Repository      │
     │              │
     ▼              │
Source Build ───────┘
                    │
                    ▼
              Plugin Registry
                    │
                    ▼
               ZDE Runtime
```

---

# 2. Goals

## 2.1 Primary Goals

### G1 — Modular Plugin Architecture

ZDE harus dapat menambahkan fitur baru tanpa harus memodifikasi atau rebuild ZDE Core.

Contoh:

- C/C++ support
- Rust support
- Python support
- Go support
- Zig support
- CMake support
- Docker integration
- Database tools
- Markdown tools

---

### G2 — Binary-first Distribution

Marketplace harus menyediakan plugin dalam bentuk prebuilt binary/package.

User melakukan:

```text
Install
   ↓
Download
   ↓
Verify
   ↓
Extract
   ↓
Register
   ↓
Ready
```

User **tidak perlu mempunyai compiler atau development environment** untuk menggunakan plugin binary.

---

### G3 — Git Source Installation

Developer dapat memasang plugin dari Git repository.

Contoh:

```bash
zde plugin install-source https://github.com/example/zde-rust
```

ZDE harus dapat:

```text
Git Clone
    ↓
Manifest Detection
    ↓
Dependency Detection
    ↓
Build Configuration
    ↓
Compilation
    ↓
Artifact Validation
    ↓
Package
    ↓
Installation
```

---

### G4 — Unified Plugin Runtime

Plugin dari Marketplace dan plugin hasil Git Source Build harus menggunakan runtime yang sama.

```text
Marketplace Plugin
       │
       ▼
       ┐
       │
       ▼
Plugin Registry
       ▲
       │
       ┘
       ▲
       │
Git Source Plugin
```

Tidak boleh ada sistem runtime berbeda untuk:

- Marketplace plugin
- Git plugin
- Local plugin
- Bundled plugin

---

### G5 — LSP Integration

Plugin dapat mendefinisikan Language Server yang digunakan ZDE.

Contoh:

```text
C/C++ Plugin
     ↓
clangd

Rust Plugin
     ↓
rust-analyzer

Python Plugin
     ↓
pyright

Go Plugin
     ↓
gopls
```

LSP harus tetap dikelola oleh ZDE Core melalui `LanguageServerManager`.

Plugin hanya mendeskripsikan bagaimana LSP digunakan.

---

# 3. Non-Goals

Fitur berikut tidak menjadi target versi awal:

- Plugin payment system
- Paid marketplace
- Plugin review system
- Plugin analytics
- Plugin social system
- Cloud build service
- Remote compilation
- Plugin code execution sandbox tingkat lanjut
- Full third-party developer portal
- Mobile plugin support

Fitur tersebut dapat dipertimbangkan pada versi berikutnya.

---

# 4. Product Principles

## 4.1 Binary First

Pengguna biasa tidak boleh dipaksa melakukan compilation.

Marketplace harus memberikan artifact siap pakai.

---

## 4.2 Source Build is an Advanced Feature

Git installation diperuntukkan untuk:

- developer
- contributor
- beta tester
- bleeding-edge user
- internal ZDE development

---

## 4.3 Plugin Runtime Must Be Source-Agnostic

Runtime tidak boleh mengetahui apakah plugin berasal dari:

```text
Marketplace
Git
Local file
Bundled with ZDE
```

Setelah installation selesai, semua plugin dianggap sebagai plugin biasa.

---

## 4.4 LSP is a Capability

LSP bukan tipe plugin tersendiri.

LSP merupakan capability yang dapat diberikan oleh plugin.

Contoh:

```text
Rust Plugin
 ├── Language
 ├── Grammar
 ├── LSP
 └── Toolchain
```

---

# 5. Plugin Types

Sistem plugin harus mendukung capability.

## 5.1 Language

Untuk menyediakan dukungan bahasa.

Contoh:

```text
C/C++
Rust
Python
Go
Zig
TypeScript
```

Capability:

```text
language
grammar
lsp
formatter
debugger
toolchain
```

---

## 5.2 Tool

Plugin yang menyediakan atau mengintegrasikan external tool.

Contoh:

```text
CMake
Ninja
clang-format
Prettier
Black
```

---

## 5.3 UI

Plugin yang menambahkan komponen UI.

Contoh:

```text
Database Explorer
Profiler
Docker Explorer
Shader Viewer
```

---

## 5.4 Native

Plugin yang membutuhkan akses native terhadap ZDE API.

Native plugin bukan prioritas versi pertama karena memiliki risiko:

- ABI compatibility
- crash propagation
- platform compatibility
- compiler compatibility
- memory safety

Native plugin sebaiknya pada tahap awal dijalankan secara isolated/out-of-process apabila memungkinkan.

---

# 6. Package Types

Sistem package minimal terdiri dari:

```text
.zdeplugin
.zdetool
```

## 6.1 `.zdeplugin`

Plugin utama.

Contoh:

```text
org.zenvra.rust.zdeplugin
```

Isi:

```text
plugin.json
language/
grammar/
lsp/
icons/
```

---

## 6.2 `.zdetool`

Package untuk executable/tool dependency.

Contoh:

```text
org.zenvra.rust-analyzer.zdetool
org.zenvra.clangd.zdetool
```

Isi dapat berupa:

```text
manifest
binary
licenses
checksums
metadata
```

---

# 7. Plugin Package Structure

Contoh:

```text
org.zenvra.rust/
│
├── plugin.json
│
├── language/
│   └── rust.json
│
├── grammar/
│   └── rust.json
│
├── lsp/
│   └── rust-analyzer.json
│
├── bin/
│   ├── windows-x64/
│   ├── linux-x64/
│   ├── macos-x64/
│   └── macos-arm64/
│
├── icons/
│   └── rust.svg
│
└── LICENSE
```

Binary tidak harus selalu berada di plugin package.

Plugin dapat mendeklarasikan dependency terhadap `.zdetool`.

---

# 8. Plugin Manifest

Format manifest awal menggunakan JSON.

Contoh:

```json
{
  "id": "org.zenvra.rust",
  "name": "Rust",
  "version": "1.0.0",

  "publisher": {
    "id": "zenvra",
    "name": "Zenvra"
  },

  "description": "Rust language support for ZDE",

  "zde": {
    "minimumVersion": "0.5.0",
    "maximumVersion": null
  },

  "platforms": [
    "windows-x64",
    "linux-x64",
    "macos-x64",
    "macos-arm64"
  ],

  "capabilities": [
    "language",
    "grammar",
    "lsp"
  ],

  "dependencies": [
    {
      "id": "org.zenvra.rust-analyzer",
      "version": ">=1.0.0"
    }
  ]
}
```

---

# 9. Language Contribution

Language plugin harus dapat mendaftarkan bahasa tanpa mengubah source code ZDE.

Contoh:

```json
{
  "id": "rust",
  "name": "Rust",

  "extensions": [
    ".rs"
  ],

  "grammar": "grammar/rust.json",

  "lsp": "lsp/rust-analyzer.json"
}
```

ZDE kemudian melakukan:

```text
.rs file
   ↓
Language Registry
   ↓
rust
   ↓
Rust Plugin
   ↓
LSP configuration
   ↓
LanguageServerManager
```

---

# 10. LSP Configuration

Plugin tidak boleh langsung mengontrol internal LSP implementation.

Plugin hanya mendeskripsikan executable dan configuration.

Contoh:

```json
{
  "id": "rust-analyzer",

  "transport": "stdio",

  "executable": {
    "windows-x64": "rust-analyzer.exe",
    "linux-x64": "rust-analyzer",
    "macos-x64": "rust-analyzer",
    "macos-arm64": "rust-analyzer"
  },

  "args": [],

  "environment": {}
}
```

ZDE Core:

```text
LanguageServerManager
        ↓
ServerRegistry
        ↓
LSP Client
        ↓
Transport
        ↓
rust-analyzer
```

---

# 11. Tool Resolution

ZDE harus mendukung tiga sumber executable.

## 11.1 System

Executable ditemukan dari system PATH.

```text
PATH
 ↓
rust-analyzer
```

---

## 11.2 Managed

ZDE menyediakan binary yang dikelola oleh ZDE.

```text
ZDE
 ↓
Tool Manager
 ↓
Managed Binary
```

---

## 11.3 Bundled

Binary berada di dalam plugin package.

```text
Plugin
 └── bin/
      └── rust-analyzer
```

Resolution priority dapat berupa:

```text
System
   ↓
Managed
   ↓
Bundled
```

atau configurable berdasarkan plugin.

---

# 12. Marketplace

Marketplace adalah catalog dan distribution service.

Marketplace tidak bertanggung jawab terhadap plugin runtime.

Marketplace menyediakan:

```text
Plugin Metadata
Plugin Versions
Compatibility
Download URLs
Checksums
Signatures
```

Contoh:

```json
{
  "plugins": [
    {
      "id": "org.zenvra.rust",
      "name": "Rust",
      "latestVersion": "1.0.0",
      "manifest": "...",
      "download": "...",
      "sha256": "..."
    }
  ]
}
```

---

# 13. Marketplace UI

UI mengikuti konsep IDE plugin manager.

```text
Settings
└── Plugins

    ┌───────────────────────────────────────┐
    │ Plugins                               │
    │                                       │
    │ [ Marketplace ] [ Installed ]         │
    │                                       │
    │ Search plugins...                    │
    │                                       │
    │ Rust                                  │
    │ Rust language support                 │
    │                                       │
    │ Completion · Diagnostics · LSP        │
    │                                       │
    │                         [ Install ]    │
    └───────────────────────────────────────┘
```

Plugin details harus menampilkan:

- Name
- Icon
- Publisher
- Description
- Version
- Compatibility
- Capabilities
- Dependencies
- Installation status
- Update status

---

# 14. Binary Installation Flow

```text
User clicks Install
        ↓
Marketplace Client
        ↓
Resolve latest compatible version
        ↓
Download package
        ↓
Verify SHA-256
        ↓
Verify signature
        ↓
Validate manifest
        ↓
Check dependencies
        ↓
Extract to temporary directory
        ↓
Validate package
        ↓
Atomic installation
        ↓
Plugin Registry
        ↓
Plugin Activated
```

Installation harus atomic.

Plugin yang gagal di-install tidak boleh meninggalkan installation setengah jadi.

---

# 15. Git Source Installation

Git installation adalah developer-oriented feature.

Command:

```bash
zde plugin install-source https://github.com/example/zde-rust.git
```

Flow:

```text
Git Repository
      ↓
Clone
      ↓
Find Manifest
      ↓
Validate Manifest
      ↓
Detect Build System
      ↓
Check Build Requirements
      ↓
Configure
      ↓
Compile
      ↓
Collect Artifact
      ↓
Validate Artifact
      ↓
Create .zdeplugin
      ↓
Install
```

---

# 16. Source Build Manifest

Repository plugin harus menyediakan build manifest.

Contoh:

```yaml
id: org.example.rust
name: Rust
version: 1.0.0

build:
  system: cmake

  requirements:
    cmake: ">=3.28"
    ninja: ">=1.11"

  configure:
    args:
      - "-DZDE_BUILD_PLUGIN=ON"

  targets:
    - zde-rust

  output:
    - "build/bin/zde-rust"
```

Tujuannya agar ZDE **tidak perlu menebak cara build repository**.

---

# 17. Supported Build Systems

Versi pertama disarankan hanya mendukung build system yang benar-benar dibutuhkan.

Prioritas:

```text
1. CMake
2. Cargo
3. CMake + Ninja
```

Build system lain dapat ditambahkan kemudian:

```text
Meson
Make
MSBuild
npm
Custom
```

---

# 18. Build Environment Detection

Sebelum compilation:

```text
CMake
Compiler
Ninja
SDK
Required libraries
```

harus diperiksa.

Contoh:

```text
Plugin requires:

CMake >= 3.28     ✓
Ninja >= 1.11     ✓
MSVC              ✓
Windows SDK       ✓

Environment ready.
```

Jika requirement tidak terpenuhi:

```text
Cannot build plugin.

Missing:
CMake >= 3.28

[Install CMake]
```

---

# 19. Source Build Isolation

Source plugin tidak boleh langsung di-build di installation directory.

Gunakan:

```text
~/.zde/build-cache/
```

Contoh:

```text
~/.zde/build-cache/
└── org.example.rust/
    ├── source/
    ├── build/
    └── artifacts/
```

Setelah build berhasil:

```text
artifacts
    ↓
validation
    ↓
package
    ↓
~/.zde/plugins/
```

Build failure tidak boleh merusak plugin yang sudah terinstall.

---

# 20. Plugin Registry

Plugin Registry menjadi sumber kebenaran mengenai plugin yang tersedia di runtime.

Registry harus dapat mengetahui:

```text
Plugin ID
Version
Source
Location
Status
Capabilities
Dependencies
```

Plugin source:

```text
Bundled
Marketplace
Local
Git
```

Contoh internal state:

```text
org.zenvra.rust
Version: 1.0.0
Source: Marketplace
Status: Enabled

Capabilities:
- language
- grammar
- lsp
```

---

# 21. Plugin Lifecycle

Minimal lifecycle:

```text
Discovered
    ↓
Validated
    ↓
Installed
    ↓
Registered
    ↓
Enabled
    ↓
Loaded
    ↓
Disabled
    ↓
Uninstalled
```

Plugin dapat:

```text
Enable
Disable
Update
Rollback
Uninstall
```

---

# 22. Compatibility

Plugin harus mendeklarasikan compatibility.

Contoh:

```json
{
  "zde": {
    "minimumVersion": "0.5.0",
    "maximumVersion": "0.7.x"
  }
}
```

ZDE harus menolak installation jika plugin tidak kompatibel.

Error harus jelas:

```text
Plugin requires ZDE >= 0.5.0

Current version:
0.4.2
```

---

# 23. Dependencies

Plugin dapat memiliki dependency.

Contoh:

```text
Rust Plugin
     │
     └── Rust Analyzer Tool
```

Dependency resolver harus:

- detect missing dependency
- detect incompatible version
- install dependency
- prevent circular dependency
- report dependency failure

---

# 24. Updates

Marketplace harus dapat memberi tahu:

```text
Installed:
Rust 1.0.0

Available:
Rust 1.1.0
```

Update flow:

```text
Check Update
      ↓
Download
      ↓
Verify
      ↓
Install new version
      ↓
Validate
      ↓
Switch version
```

Versi lama sebaiknya dapat dipertahankan sementara untuk rollback.

---

# 25. Security Requirements

Plugin package merupakan executable content.

Minimal requirement:

```text
HTTPS
SHA-256
Package validation
Path traversal protection
Manifest validation
Publisher identity
Digital signature
```

Installation flow:

```text
Download
   ↓
Hash verification
   ↓
Signature verification
   ↓
Manifest validation
   ↓
Compatibility check
   ↓
Install
```

ZDE tidak boleh menjalankan executable dari package yang gagal verification.

---

# 26. Storage Layout

Contoh:

```text
~/.zde/
│
├── plugins/
│   ├── bundled/
│   │
│   ├── marketplace/
│   │   └── org.zenvra.rust/
│   │
│   └── source/
│       └── org.example.plugin/
│
├── tools/
│   └── org.zenvra.rust-analyzer/
│
├── cache/
│
└── build-cache/
```

Lokasi final dapat disesuaikan dengan OS.

---

# 27. Bundled Plugins

ZDE Core dapat memiliki plugin bawaan.

Contoh:

```text
ZDE Installation
│
└── plugins/
    └── bundled/
        ├── org.zenvra.cpp
        ├── org.zenvra.markdown
        └── org.zenvra.git
```

Bundled plugin menggunakan API dan registry yang sama dengan marketplace plugin.

Dengan demikian fitur bawaan dan third-party plugin tidak mempunyai architecture berbeda.

---

# 28. CLI

CLI minimal:

```bash
zde plugin list
zde plugin install <plugin-id>
zde plugin uninstall <plugin-id>
zde plugin update <plugin-id>
zde plugin enable <plugin-id>
zde plugin disable <plugin-id>
```

Source:

```bash
zde plugin install-source <git-url>
```

Development:

```bash
zde plugin build <path>
zde plugin package <path>
```

---

# 29. Error Handling

Error harus actionable.

Contoh:

```text
Plugin installation failed.

Plugin:
org.example.rust

Reason:
Required ZDE version is >= 0.5.0

Current ZDE:
0.4.2

Action:
Update ZDE and retry.
```

Build error:

```text
Plugin build failed.

Build system:
CMake

Missing dependency:
Ninja >= 1.11

Source:
C:\...\plugin

Build logs:
[Open Build Logs]
```

---

# 30. MVP Scope

## Phase 1 — Plugin Foundation

Target:

```text
PluginManifest
PluginRegistry
PluginManager
PluginInstaller
```

Support:

- plugin discovery
- manifest validation
- install
- uninstall
- enable/disable

---

## Phase 2 — Language Plugin

Support:

```text
language
grammar
lsp
```

Migrasikan satu bahasa existing ke plugin.

Rekomendasi:

```text
C/C++ + clangd
```

Jika C/C++ berhasil menjadi plugin, architecture dianggap berhasil.

---

## Phase 3 — Binary Package

Implement:

```text
.zdeplugin
.zdetool
```

Support:

- package installation
- checksum
- platform detection
- dependency resolution

---

## Phase 4 — Marketplace

Implement:

```text
MarketplaceClient
Marketplace API
Plugin Search
Plugin Details
Install
Update
```

Marketplace versi pertama dapat menggunakan static JSON + artifact hosting.

Tidak perlu backend kompleks.

---

## Phase 5 — Git Source Build

Implement:

```text
Git clone
Manifest detection
Build environment detection
CMake build
Artifact packaging
Installation
```

Versi pertama fokus pada:

```text
CMake
C++
```

---

## Phase 6 — Tool Management

Implement:

```text
System Tool
Managed Tool
Bundled Tool
```

LSP binary dapat dikelola melalui Tool Manager.

---

# 31. Example: C++ Plugin

User membuka:

```text
main.cpp
```

Flow:

```text
main.cpp
    ↓
Language Registry
    ↓
cpp
    ↓
C++ Plugin
    ↓
clangd configuration
    ↓
Tool Resolver
    ↓
System / Managed / Bundled clangd
    ↓
LanguageServerManager
    ↓
clangd
```

ZDE Core tidak memiliki:

```cpp
if (extension == ".cpp")
    startClangd();
```

Semua language contribution berasal dari plugin.

---

# 32. Example: Rust Plugin from Marketplace

User:

```text
Settings
 → Plugins
 → Marketplace
 → Rust
 → Install
```

ZDE:

```text
Download Rust Plugin
        ↓
Download rust-analyzer dependency
        ↓
Verify packages
        ↓
Install
        ↓
Register rust language
        ↓
Register grammar
        ↓
Register LSP
```

Kemudian user membuka:

```text
main.rs
```

ZDE otomatis:

```text
Rust Plugin
    ↓
rust-analyzer
    ↓
LSP
```

---

# 33. Example: Developer Git Installation

Developer menjalankan:

```bash
zde plugin install-source \
    https://github.com/example/zde-rust.git
```

ZDE:

```text
Clone repository
      ↓
Read plugin manifest
      ↓
Detect CMake
      ↓
Check compiler
      ↓
Configure
      ↓
Build
      ↓
Collect artifact
      ↓
Generate .zdeplugin
      ↓
Install
```

Setelah selesai:

```text
Installed:

org.example.rust
Version: 1.0.0
Source: Git
Status: Enabled
```

Runtime tidak peduli plugin berasal dari Git.

---

# 34. Architecture Summary

```text
                         ZDE
                          │
             ┌────────────┴────────────┐
             │                         │
        Plugin System              Language System
             │                         │
      ┌──────┼───────┐                 │
      │      │       │                 │
   Registry Installer Runtime      Language Registry
      │      │                         │
      │      ├── Binary                │
      │      └── Source Build          │
      │                                │
      └──────────────┬─────────────────┘
                     │
                Capabilities
                     │
          ┌──────────┼───────────┐
          │          │           │
       Grammar      LSP       Toolchain
                     │
                     ▼
            LanguageServerManager
                     │
                     ▼
                  LSP Binary
```

---

# 35. Design Decision

## Recommended

```text
Marketplace → prebuilt binary
Git → source build
Local → package install
Bundled → package
```

Semua berakhir:

```text
Plugin Installer
       ↓
Plugin Registry
       ↓
Plugin Runtime
```

LSP:

```text
Plugin
   ↓
LSP Configuration
   ↓
Tool Resolver
   ↓
LanguageServerManager
   ↓
Binary
```

---

# 36. Success Criteria

MVP dianggap berhasil apabila:

### Plugin

- [ ] ZDE dapat menemukan plugin dari directory.
- [ ] ZDE dapat membaca manifest.
- [ ] ZDE dapat install/uninstall plugin.
- [ ] ZDE dapat enable/disable plugin.
- [ ] Plugin tidak membutuhkan perubahan source ZDE untuk ditambahkan.

### Language

- [ ] C/C++ dapat dipindahkan menjadi bundled plugin.
- [ ] Grammar dapat didaftarkan melalui plugin.
- [ ] LSP dapat didaftarkan melalui plugin.
- [ ] `clangd` dapat dijalankan melalui plugin configuration.

### Binary

- [ ] `.zdeplugin` dapat di-install.
- [ ] Platform dapat dideteksi.
- [ ] Checksum dapat diverifikasi.
- [ ] Dependency dapat di-resolve.

### Git

- [ ] Git repository dapat di-clone.
- [ ] Manifest dapat dideteksi.
- [ ] Build requirement dapat diperiksa.
- [ ] CMake project dapat di-build.
- [ ] Artifact dapat dipackage.
- [ ] Hasil build dapat di-install melalui Plugin Registry yang sama.

### Marketplace

- [ ] Plugin dapat dicari.
- [ ] Plugin details dapat ditampilkan.
- [ ] Compatible version dapat dipilih.
- [ ] Plugin dapat di-install.
- [ ] Plugin dapat di-update.

---

# 37. Future Direction

Setelah MVP stabil, sistem dapat dikembangkan menjadi:

```text
Zenvra Marketplace
│
├── Public Plugins
├── Private Repositories
├── Publisher Accounts
├── Plugin Signing
├── Version Channels
│   ├── Stable
│   ├── Beta
│   └── Nightly
│
├── Reviews
├── Downloads
└── Developer Portal
```

Namun seluruh fitur tersebut bergantung pada kestabilan **Plugin Manifest + Package Format + Plugin Registry**.

Karena itu ketiga komponen tersebut harus dianggap sebagai foundation layer dan dirancang dengan versioning sejak awal.