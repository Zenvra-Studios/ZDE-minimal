# ZDE Plugin, Toolchain & Marketplace
## Product Requirements Document (PRD)

**Product:** Zenvra Development Studio (ZDE)  
**Status:** Draft  
**Architecture Direction:** Local-First / Git-First  
**Primary Language:** C++20  
**Target:** Windows / Linux / macOS

---

# 1. Overview

ZDE membutuhkan sistem plugin yang memungkinkan dukungan bahasa pemrograman, tooling, editor feature, UI component, dan integrasi development environment ditambahkan tanpa mengubah core ZDE.

Sistem plugin ZDE dirancang dengan prinsip:

- **Local-first**
- **Git-first**
- **Binary installation bukan requirement pada fase awal**
- **Tidak bergantung pada cloud**
- **Toolchain dapat menggunakan tool yang sudah terpasang di system**
- **Plugin dapat menyediakan build recipe untuk tool yang dibutuhkan**
- **Semua metode instalasi menggunakan Plugin Registry yang sama**
- **Marketplace merupakan layer tambahan, bukan fondasi sistem plugin**

Pada fase awal, user dapat memasang plugin menggunakan repository Git:

```text
Git Repository
      ↓
ZDE Plugin Installer
      ↓
Clone
      ↓
Read Manifest
      ↓
Resolve Dependencies
      ↓
Detect Toolchain
      ↓
Build
      ↓
Install
      ↓
Register
      ↓
Activate
```

---

# 2. Goals

## 2.1 Plugin System

Menyediakan sistem plugin modular yang dapat:

- install plugin
- uninstall plugin
- enable/disable plugin
- update plugin
- load plugin
- resolve dependency
- validate compatibility
- register plugin
- build plugin dari source

---

## 2.2 Language Support

Language support harus dapat dikembangkan sebagai plugin.

Contoh:

```text
org.zenvra.cpp
org.zenvra.rust
org.zenvra.python
org.zenvra.typescript
org.zenvra.go
```

Plugin bahasa dapat menyediakan:

- syntax grammar
- language configuration
- file extensions
- LSP configuration
- formatter
- debugger integration
- compiler/toolchain integration
- editor features

---

## 2.3 Toolchain Management

ZDE harus mampu menggunakan executable yang sudah tersedia di system.

Contoh:

```text
clangd
rust-analyzer
rustc
cargo
clang
gcc
g++
bun
node
python
gopls
```

Prioritas resolver:

```text
1. Project-local tool
2. ZDE managed/local tool
3. System PATH
4. Plugin-provided build/install recipe
```

ZDE **tidak wajib membundel compiler/toolchain**.

---

# 3. Non-Goals

Untuk fase awal, ZDE **tidak akan membangun**:

- PostgreSQL marketplace
- cloud database
- object storage
- CDN
- online payment
- publisher billing
- cloud build service
- remote compilation
- analytics marketplace
- review/rating system
- download counter
- mandatory user account
- centralized plugin authentication

Semua fitur tersebut dapat ditambahkan pada fase berikutnya tanpa mengubah core Plugin System.

---

# 4. Core Architecture

```text
                         ZDE Core
                            │
                    ┌───────┴────────┐
                    │                │
              Plugin Manager    Tool Manager
                    │                │
                    └───────┬────────┘
                            │
                     Plugin Registry
                            │
              ┌─────────────┼─────────────┐
              │             │             │
           Bundled         Git          Local
              │             │             │
              └─────────────┼─────────────┘
                            │
                     Installed Plugin
                            │
              ┌─────────────┼─────────────┐
              │             │             │
           Grammar         LSP        Toolchain
```

---

# 5. Installation Sources

ZDE akan mendukung beberapa installation source.

## 5.1 Bundled

Plugin yang disertakan bersama ZDE.

Contoh:

```text
org.zenvra.core
org.zenvra.cpp
```

Bundled plugin tetap diperlakukan sebagai plugin biasa oleh Registry.

---

## 5.2 Git

Source utama pada fase awal.

Contoh:

```bash
zde plugin install https://github.com/Zenvra-Studios/zde-cpp.git
```

Flow:

```text
Git URL
  ↓
git clone
  ↓
manifest detection
  ↓
validation
  ↓
dependency resolution
  ↓
toolchain detection
  ↓
build
  ↓
artifact validation
  ↓
installation
```

---

## 5.3 Local

Developer dapat memasang plugin dari directory lokal.

```bash
zde plugin install ./my-plugin
```

Berguna untuk development plugin.

---

## 5.4 Marketplace — Future

Marketplace nantinya menjadi discovery/distribution layer.

Pada fase awal, Marketplace dapat berupa static catalog.

Contoh:

```json
{
  "plugins": [
    {
      "id": "org.zenvra.cpp",
      "name": "C/C++",
      "repository": "https://github.com/Zenvra-Studios/zde-cpp.git"
    }
  ]
}
```

ZDE cukup mengambil catalog tersebut lalu tetap melakukan:

```text
Marketplace
     ↓
Git Repository
     ↓
ZDE Git Installer
```

Dengan demikian Marketplace tidak memiliki dependency terhadap Plugin Runtime.

---

# 6. Plugin Package Model

Plugin memiliki manifest utama:

```text
plugin.json
```

Contoh:

```json
{
  "id": "org.zenvra.cpp",
  "name": "C/C++ Support",
  "version": "0.1.0",
  "publisher": "Zenvra Studios",

  "zde": {
    "minimumVersion": "0.1.0"
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
    "lsp",
    "toolchain"
  ],

  "dependencies": {
    "plugins": [],
    "tools": [
      "clangd"
    ]
  }
}
```

---

# 7. Plugin Directory Structure

Contoh:

```text
zde-cpp/
│
├── plugin.json
├── README.md
├── LICENSE
│
├── language/
│   └── cpp.json
│
├── grammar/
│   ├── cpp.json
│   └── c.json
│
├── lsp/
│   └── clangd.json
│
├── build/
│   └── ...
│
└── src/
    └── ...
```

Plugin tidak wajib memiliki semua directory.

---

# 8. Plugin Manifest

Manifest harus mendeskripsikan:

```text
Identity
Compatibility
Capabilities
Dependencies
Build
Language
LSP
Tools
Platform
```

Contoh:

```json
{
  "id": "org.zenvra.rust",
  "name": "Rust Support",
  "version": "0.1.0",

  "capabilities": [
    "language",
    "grammar",
    "lsp"
  ],

  "dependencies": {
    "tools": [
      "rustc",
      "cargo",
      "rust-analyzer"
    ]
  }
}
```

---

# 9. Build System

ZDE harus dapat mendeteksi build system plugin.

Prioritas awal:

```text
CMake
Cargo
CMake + Ninja
```

Future:

```text
Meson
Make
Gradle
npm
Bun
Custom build script
```

Plugin dapat menyediakan build manifest.

Contoh:

```yaml
build:
  system: cmake

  requirements:
    - cmake
    - ninja
    - cxx

  configure:
    - "-G"
    - "Ninja"

  targets:
    - zde-plugin

  output:
    directory: build/bin
```

---

# 10. Source Build Flow

Ketika user menginstall plugin dari Git:

```text
1. Clone repository
        ↓
2. Find plugin.json
        ↓
3. Validate manifest
        ↓
4. Check ZDE compatibility
        ↓
5. Resolve dependencies
        ↓
6. Detect required tools
        ↓
7. Detect build system
        ↓
8. Configure
        ↓
9. Compile
        ↓
10. Validate artifact
        ↓
11. Package/install
        ↓
12. Register
        ↓
13. Activate
```

Build dilakukan di temporary/cache directory.

Contoh:

```text
~/.zde/
└── build-cache/
    └── org.zenvra.cpp/
```

Plugin hanya dipindahkan ke installation directory setelah build berhasil.

---

# 11. Toolchain Resolution

Toolchain adalah dependency terpisah dari plugin.

Contoh:

```text
org.zenvra.cpp
        │
        └── requires clangd
```

Tool resolver:

```text
Tool Requirement
       ↓
Project Local?
       │
      NO
       ↓
ZDE Local Tool?
       │
      NO
       ↓
System PATH?
       │
      NO
       ↓
Build / Install Recipe
```

---

# 12. System Tool

Jika user sudah memiliki:

```bash
clangd
```

ZDE tidak perlu menginstall clangd kedua.

Contoh:

```text
Checking clangd...

✓ Found:
  C:\LLVM\bin\clangd.exe

Using system clangd.
```

Tool Manager menyimpan informasi:

```text
Tool:
clangd

Source:
System

Executable:
C:\LLVM\bin\clangd.exe

Version:
20.x
```

---

# 13. Managed / Local Tool

Jika tool tidak ditemukan, plugin dapat menyediakan recipe untuk mendapatkan/build tool.

Contoh:

```text
org.zenvra.cpp
        ↓
requires clangd
        ↓
clangd tidak ditemukan
        ↓
check tool recipe
        ↓
build/install
        ↓
~/.zde/tools/clangd/
```

ZDE kemudian menggunakan executable tersebut tanpa mempengaruhi system installation user.

---

# 14. Important: LLVM

ZDE **tidak boleh otomatis meng-compile seluruh LLVM hanya karena user menginstall C/C++ support**, kecuali user memang memilih source-build toolchain.

Default:

```text
C/C++ Plugin
      ↓
Check clangd
      ↓
System clangd?
      ↓
Use it
```

Jika tidak tersedia:

```text
clangd source/build recipe
      ↓
Build only required component
```

atau future:

```text
Prebuilt managed tool
```

---

# 15. Rust Toolchain

Rust support dapat mendeklarasikan:

```text
rustc
cargo
rust-analyzer
```

Resolver:

```text
Project
  ↓
Local Rust toolchain
  ↓
System rustc/cargo
  ↓
Managed/local tool
```

ZDE tidak harus menggantikan `rustup`.

Jika user sudah memiliki:

```text
rustup
rustc
cargo
rust-analyzer
```

ZDE cukup mendeteksi dan menggunakannya.

---

# 16. JavaScript / TypeScript / Bun

Contoh dependency:

```json
{
  "dependencies": {
    "tools": [
      "bun",
      "vtsls"
    ]
  }
}
```

Resolver dapat mencari:

```text
bun
node
vtsls
tsserver
```

Jika tersedia di system:

```text
Use System Tool
```

Jika tidak:

```text
Use project-local tool
```

atau future:

```text
Managed Tool
```

---

# 17. Language Server Architecture

LSP tetap berada di luar proses ZDE.

```text
ZDE
 │
 │ JSON-RPC / stdio
 ▼
Language Server
```

Contoh:

```text
C/C++
  ↓
clangd

Rust
  ↓
rust-analyzer

Go
  ↓
gopls

Python
  ↓
pyright

TypeScript
  ↓
vtsls / tsserver
```

Core component:

```text
LanguageServerManager
ServerRegistry
DocumentSyncManager
LSP Client
Transport
Protocol
```

---

# 18. LSP Resolution

Language plugin tidak harus mengetahui absolute path executable.

Contoh:

```json
{
  "language": "cpp",
  "server": "clangd",
  "transport": "stdio"
}
```

ZDE melakukan:

```text
Language Plugin
      ↓
LSP Requirement
      ↓
Tool Manager
      ↓
Resolve clangd
      ↓
Executable Path
      ↓
LanguageServerManager
      ↓
Launch
```

Dengan demikian:

```text
Plugin ≠ Tool ≠ LSP Process
```

Ketiganya tetap terpisah.

---

# 19. Syntax Highlighting

ZDE menggunakan dua layer:

```text
Layer 1:
Data-driven Grammar
        +
Layer 2:
LSP Semantic Tokens
```

Grammar tidak boleh bergantung pada hardcoded C++ lexer.

Contoh:

```text
grammar/cpp.json
```

Generic Grammar Engine bertanggung jawab terhadap lexical/syntactic highlighting.

LSP memberikan semantic information.

---

# 20. Plugin Registry

Plugin Registry adalah registry **lokal**.

Tidak membutuhkan server.

Registry menyimpan:

```text
Plugin ID
Name
Version
Source
Location
Status
Capabilities
Dependencies
```

Source:

```text
Bundled
Git
Local
Marketplace (future)
```

Lifecycle:

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
```

Optional states:

```text
Disabled
Failed
Uninstalled
```

---

# 21. Tool Registry

Tool Registry juga bersifat lokal.

Contoh:

```text
Tool ID:
org.zenvra.clangd

Executable:
C:\LLVM\bin\clangd.exe

Source:
System

Version:
20.1.8

Status:
Available
```

Source:

```text
System
Project
Local
Managed
Bundled
```

Pada fase awal, registry tidak membutuhkan database cloud.

---

# 22. Dependency Resolver

Dependency resolver menangani:

```text
Plugin → Plugin
Plugin → Tool
Tool → Tool
```

Contoh:

```text
org.zenvra.cpp
       │
       └── clangd

org.zenvra.rust
       ├── rustc
       ├── cargo
       └── rust-analyzer
```

Resolver harus:

- mendeteksi missing dependency
- mendeteksi version conflict
- menentukan source
- menentukan installation order
- menolak circular dependency

---

# 23. Installation Directory

Default local storage:

```text
~/.zde/
│
├── plugins/
│   ├── org.zenvra.cpp/
│   └── org.zenvra.rust/
│
├── tools/
│   ├── clangd/
│   └── rust-analyzer/
│
├── cache/
│
├── build-cache/
│
└── registry/
```

Registry dapat menggunakan:

```text
JSON
SQLite
```

dan **bukan cloud database**.

---

# 24. Atomic Installation

Plugin tidak boleh dianggap installed sebelum seluruh proses berhasil.

```text
Git Clone
   ↓
Build Directory
   ↓
Validation
   ↓
Temporary Install
   ↓
Final Rename
```

Contoh:

```text
plugins/
└── .staging/
    └── org.zenvra.cpp/
```

Setelah sukses:

```text
.staging/org.zenvra.cpp
        ↓
plugins/org.zenvra.cpp
```

Jika build gagal:

```text
remove staging
```

Plugin existing tidak boleh rusak.

---

# 25. CLI

Initial CLI:

```bash
zde plugin list
zde plugin install <git-url>
zde plugin uninstall <plugin-id>
zde plugin enable <plugin-id>
zde plugin disable <plugin-id>
zde plugin update <plugin-id>
```

Development:

```bash
zde plugin build <path>
zde plugin package <path>
zde plugin validate <path>
```

Toolchain:

```bash
zde tool list
zde tool locate <tool>
zde tool info <tool>
```

Future Marketplace:

```bash
zde marketplace search <query>
zde marketplace install <plugin-id>
```

---

# 26. Developer Workflow

Developer dapat membuat plugin:

```text
my-zde-plugin/
├── plugin.json
├── src/
├── grammar/
├── lsp/
└── build/
```

Kemudian:

```bash
zde plugin validate .
zde plugin build .
zde plugin install .
```

Atau langsung:

```bash
zde plugin install https://github.com/user/my-zde-plugin.git
```

---

# 27. Marketplace — Future Architecture

Marketplace bukan bagian wajib dari Plugin Core.

Phase awal:

```text
GitHub
   ↓
Git Clone
   ↓
Plugin Installer
```

Phase berikutnya:

```text
Static Marketplace Catalog
   ↓
Git Repository
   ↓
Plugin Installer
```

Phase mature:

```text
Zenvra Marketplace API
        ↓
Catalog / Database
        ↓
Artifact Storage
        ↓
CDN
        ↓
ZDE
```

Core plugin system tidak boleh bergantung langsung terhadap layer tersebut.

---

# 28. Marketplace Static Catalog

MVP Marketplace dapat berupa static JSON:

```json
{
  "schemaVersion": 1,

  "plugins": [
    {
      "id": "org.zenvra.cpp",
      "name": "C/C++ Support",
      "version": "0.1.0",
      "repository": "https://github.com/Zenvra-Studios/zde-cpp.git"
    }
  ]
}
```

ZDE dapat:

```text
Download catalog
      ↓
Display plugins
      ↓
User clicks Install
      ↓
Clone Git repository
      ↓
Normal plugin installation flow
```

Tidak ada binary hosting yang wajib.

---

# 29. Security

Karena Git source build menjalankan code secara lokal, ZDE harus memperlakukan source plugin sebagai potentially untrusted.

Minimum:

- tampilkan repository sebelum install
- tampilkan build command
- tampilkan dependency
- tampilkan toolchain yang akan digunakan
- jangan menjalankan arbitrary installer secara diam-diam
- build dilakukan pada temporary directory
- validasi manifest
- validasi output
- error handling yang jelas

Future:

```text
Plugin Signature
Publisher Verification
Trusted Publishers
Sandboxed Plugin Host
Permission System
```

---

# 30. Native Plugin Architecture

Native C++ plugin dapat memiliki ABI compatibility issue.

Untuk fase awal:

```text
ZDE Version
    ↓
Plugin Compatibility Check
    ↓
Native Plugin
```

Namun architecture harus disiapkan agar future dapat menggunakan:

```text
ZDE
 │
 └── Plugin Host Process
          │
          └── Native Plugin
```

Pendekatan out-of-process akan mengurangi risiko crash plugin menghancurkan seluruh ZDE.

---

# 31. Initial Plugin Types

## Language Plugin

```text
C/C++
Rust
Python
Go
TypeScript
```

## Tool Plugin

```text
clangd
rust-analyzer
gopls
pyright
vtsls
```

## UI Plugin

```text
Panels
Editors
Views
Commands
Themes
```

## Native Plugin

Plugin dengan native C++ implementation.

---

# 32. Example: C/C++ Installation

User:

```bash
zde plugin install https://github.com/Zenvra-Studios/zde-cpp.git
```

ZDE:

```text
Clone repository
      ↓
Read plugin.json
      ↓
C/C++ Support detected
      ↓
Requires clangd
      ↓
Search clangd
      ↓
System clangd found
      ↓
Build ZDE C++ plugin
      ↓
Install
      ↓
Register
      ↓
Activate
```

Result:

```text
C/C++ Support: Enabled
LSP: clangd
Source: System
Status: Running
```

---

# 33. Example: Rust Installation

```bash
zde plugin install https://github.com/Zenvra-Studios/zde-rust.git
```

Resolver:

```text
rustc
cargo
rust-analyzer
```

Jika tersedia:

```text
✓ rustc
✓ cargo
✓ rust-analyzer
```

Plugin langsung menggunakan toolchain tersebut.

---

# 34. Example: Missing Toolchain

Jika:

```text
clangd: NOT FOUND
```

ZDE menampilkan:

```text
C/C++ Support requires clangd.

Options:

[Use System Installation]
[Build/Install Managed Tool]
[Cancel]
```

Pada fase awal, user tetap dapat menginstall clangd secara manual dan kemudian ZDE melakukan re-scan.

---

# 35. Product Principles

### Principle 1 — Local First

ZDE harus tetap berguna tanpa koneksi internet setelah plugin/tool tersedia.

### Principle 2 — Git First

Git adalah distribution mechanism utama pada fase awal.

### Principle 3 — System Tool Friendly

Jangan memaksa user menginstall toolchain kedua jika system tool sudah tersedia.

### Principle 4 — Plugin and Tool Separation

```text
Plugin
    ≠
Tool
```

### Principle 5 — Marketplace Is Optional

Marketplace adalah discovery/distribution layer, bukan dependency Plugin Runtime.

### Principle 6 — Same Runtime

Apapun sumber plugin:

```text
Bundled
Git
Local
Marketplace
```

hasil akhirnya harus masuk ke:

```text
Plugin Registry
      ↓
Plugin Manager
      ↓
Plugin Runtime
```

---

# 36. Development Phases

## Phase 1 — Plugin Foundation

Implement:

- Plugin manifest
- Plugin Registry
- Plugin Manager
- lifecycle
- validation
- enable/disable
- local installation

---

## Phase 2 — Git Installer

Implement:

```text
git clone
repository detection
manifest detection
build detection
build
install
```

CLI:

```bash
zde plugin install <git-url>
```

---

## Phase 3 — Tool Registry

Implement:

```text
Tool Registry
Tool Resolver
PATH detection
version detection
project-local tools
```

---

## Phase 4 — LSP Integration

Implement:

```text
LanguageServerManager
ServerRegistry
LSP Client
Transport
Document Sync
```

Integrasikan:

```text
clangd
rust-analyzer
gopls
pyright
vtsls
```

---

## Phase 5 — First Real Language Plugins

Target:

```text
org.zenvra.cpp
org.zenvra.rust
org.zenvra.python
```

C/C++ support harus dapat dipindahkan dari core menjadi bundled plugin.

---

## Phase 6 — Static Marketplace

Implement:

```text
marketplace.json
```

Marketplace hanya berisi:

```text
metadata
Git repository URL
compatibility
version
description
```

Tidak membutuhkan database.

---

## Phase 7 — Managed Tools

Tambahkan optional:

```text
~/.zde/tools/
```

dengan kemampuan:

- managed executable
- tool version
- source
- install
- remove
- update

---

## Phase 8 — Full Marketplace

Jika ZDE sudah membutuhkan ecosystem besar:

```text
Marketplace API
Database
Publisher
Artifact Storage
CDN
Signing
Channels
Reviews
```

Semua ditambahkan di atas architecture yang sudah ada.

---

# 37. MVP Success Criteria

MVP dianggap berhasil jika:

### Plugin

```bash
zde plugin install <git-url>
```

dapat:

```text
clone
→ validate
→ build
→ install
→ register
→ activate
```

### Toolchain

ZDE dapat mendeteksi:

```text
clangd
rustc
cargo
rust-analyzer
bun
node
```

dari system/project environment.

### LSP

Minimal:

```text
C/C++ → clangd
```

dapat dijalankan melalui:

```text
LanguageServerManager
```

### Architecture

C/C++ support dapat dipindahkan menjadi:

```text
org.zenvra.cpp
```

tanpa mengubah core editor secara besar.

### Marketplace

Belum membutuhkan database.

Static catalog + Git repository sudah cukup.

---

# 38. Final Architecture

Target architecture fase awal:

```text
                       ZDE
                        │
              ┌─────────┴─────────┐
              │                   │
        Plugin Manager       Tool Manager
              │                   │
              └─────────┬─────────┘
                        │
                 Local Registry
                        │
          ┌─────────────┼─────────────┐
          │             │             │
       Bundled          Git          Local
          │             │             │
          └─────────────┼─────────────┘
                        │
                     Plugin
                        │
             ┌──────────┼──────────┐
             │          │          │
          Grammar      LSP      Commands/UI
                        │
                        ▼
                Tool Resolution
                        │
             ┌──────────┼──────────┐
             │          │          │
           Project     Local      System
             │          │          │
             └──────────┼──────────┘
                        │
                   Executable
```

Future:

```text
                    Zenvra Marketplace
                           │
                    Static Catalog
                           │
                     Git Repository
                           │
                           ▼
                         ZDE
                           │
                    Local Registry
```

Dan jauh di masa depan:

```text
Zenvra Marketplace
      │
      ├── API
      ├── Database
      ├── Artifact Storage
      ├── CDN
      ├── Publisher System
      └── Signing
             │
             ▼
            ZDE
```

**Kesimpulan arsitektur:** ZDE tidak boleh bergantung pada Marketplace. Marketplace bergantung pada kemampuan instalasi/plugin system ZDE.