# ZDE — Run, Build & Toolchain Service

**Project:** ZDE-minimal  
**Document Type:** Product Requirements Document (PRD)  
**Status:** Proposed  
**Target:** Native C++ IDE  
**Primary Goal:** Menyediakan sistem Build, Run, Project Detection, Toolchain Detection, dan Process Management yang terintegrasi dengan toolbar ZDE dengan workflow yang menyerupai IDE modern seperti JetBrains.

---

# 1. Overview

ZDE membutuhkan sistem terpusat untuk menjalankan dan membangun project tanpa mengikat toolbar terhadap bahasa pemrograman atau build system tertentu.

Sistem harus mampu:

1. Mendeteksi tipe project secara otomatis.
2. Mendeteksi build system yang digunakan.
3. Mendeteksi toolchain yang tersedia.
4. Membuat Run Configuration secara otomatis.
5. Melakukan build sebelum menjalankan program.
6. Menentukan artifact hasil compilation/build.
7. Menjalankan executable/interpreter.
8. Menangkap `stdout`, `stderr`, dan exit code.
9. Menghentikan process yang dijalankan ZDE.
10. Menyediakan konfigurasi toolchain secara manual melalui Settings.
11. Mendukung bundled toolchain dan system toolchain.
12. Dapat diperluas ke bahasa/build system baru tanpa mengubah `RunService`.

Konsep utama:

```text
Project
   ↓
Project Detection
   ↓
Build System Detection
   ↓
Toolchain Detection
   ↓
Run Configuration
   ↓
Build
   ↓
Artifact Resolution
   ↓
Run
   ↓
Process Management
```

---

# 2. Product Vision

ZDE bukan sekadar text editor yang memiliki tombol `Compile`.

ZDE harus memiliki konsep:

> **"Open Project → ZDE understands the project → Configure → Build → Run."**

Toolbar hanya menjadi interface terhadap service.

Toolbar tidak boleh mengetahui detail:

- CMake
- Cargo
- Maven
- Python
- Free Pascal
- MSVC
- LLVM
- GCC
- JDK

Semua logic tersebut harus berada di service layer.

---

# 3. Scope

## 3.1 In Scope

### Core

- Process execution
- Process termination
- stdout/stderr capture
- exit code
- environment variables
- working directory
- asynchronous process execution

### Project Detection

- CMake
- Cargo/Rust
- Maven/Java
- Gradle
- Make
- Python
- Pascal
- Plain C/C++
- Plain Java

### Build Systems

- CMake
- Cargo
- Maven
- Gradle
- Make
- Free Pascal
- Direct compiler mode

### Interpreters

- Python

### Toolchains

- LLVM/Clang
- MSVC
- MinGW/GCC
- Rust/Cargo
- JDK
- Maven
- Gradle
- Python
- Free Pascal

### Run Configuration

- Automatic configuration
- Manual configuration
- Arguments
- Working directory
- Environment variables
- Build before run
- Selected target

### UI

- Run
- Stop
- Run configuration selector
- Build output
- Process output
- Toolchain settings

---

# 4. Non-Goals

Versi pertama tidak wajib menyediakan:

- Debugger
- Remote debugger
- Distributed build
- Docker integration
- WSL integration
- SSH deployment
- Android toolchain
- iOS toolchain
- Full IDE debugger
- Automatic package installation
- Automatic compiler download tanpa persetujuan user

Debugger dapat ditambahkan sebagai service terpisah pada fase berikutnya.

---

# 5. Architecture

## 5.1 High-Level Architecture

```text
                         ┌───────────────┐
                         │   Toolbar     │
                         └───────┬───────┘
                                 │
                                 ▼
                         ┌───────────────┐
                         │  RunService   │
                         └───────┬───────┘
                                 │
             ┌───────────────────┼───────────────────┐
             │                   │                   │
             ▼                   ▼                   ▼
      RunConfiguration      BuildService       ProcessManager
             │                   │                   │
             │           ┌───────┼────────┐          │
             │           │       │        │          │
             ▼           ▼       ▼        ▼          ▼
      Configuration    CMake   Cargo    Maven     OS Process
                         │       │        │
                         ▼       ▼        ▼
                      Toolchain / Command
```

---

# 6. Proposed Source Structure

```text
Source/
├── Application/
│
├── Services/
│   │
│   ├── Project/
│   │   ├── ProjectService.hpp
│   │   ├── ProjectService.cpp
│   │   ├── ProjectDetector.hpp
│   │   ├── ProjectDetector.cpp
│   │   ├── ProjectInfo.hpp
│   │   └── ProjectType.hpp
│   │
│   ├── Build/
│   │   ├── BuildService.hpp
│   │   ├── BuildService.cpp
│   │   ├── BuildSystem.hpp
│   │   ├── BuildSystemDetector.hpp
│   │   ├── CMakeBuildSystem.hpp
│   │   ├── CargoBuildSystem.hpp
│   │   ├── MavenBuildSystem.hpp
│   │   ├── GradleBuildSystem.hpp
│   │   ├── MakeBuildSystem.hpp
│   │   └── FreePascalBuildSystem.hpp
│   │
│   ├── Run/
│   │   ├── RunService.hpp
│   │   ├── RunService.cpp
│   │   ├── RunConfiguration.hpp
│   │   ├── RunConfigurationStore.hpp
│   │   └── RunConfigurationResolver.hpp
│   │
│   ├── Process/
│   │   ├── ProcessRunner.hpp
│   │   ├── ProcessRunner.cpp
│   │   ├── ProcessHandle.hpp
│   │   ├── ProcessSpec.hpp
│   │   └── ProcessResult.hpp
│   │
│   ├── Toolchain/
│   │   ├── Toolchain.hpp
│   │   ├── ToolchainManager.hpp
│   │   ├── ToolchainDetector.hpp
│   │   ├── CppToolchain.hpp
│   │   ├── RustToolchain.hpp
│   │   ├── JavaToolchain.hpp
│   │   ├── PythonToolchain.hpp
│   │   └── FreePascalToolchain.hpp
│   │
│   └── Output/
│       ├── OutputService.hpp
│       └── BuildOutput.hpp
│
├── UI/
│   ├── Toolbar/
│   ├── BuildPanel/
│   ├── RunConfiguration/
│   └── Settings/
│
└── Config/
```

---

# 7. Project Detection

## 7.1 Principles

Project detection dalam ZDE harus menganut prinsip **Non-Exclusive Polyglot & Multi-Target Detection**:

1. **Manifest First, File Content Second, Extension Fallback**:
   Project detection harus menggunakan **project marker/manifest** terlebih dahulu (`CMakeLists.txt`, `Cargo.toml`, `pyproject.toml`, `pom.xml`, `.lpi`). Extension file tunggal (`.cpp`, `.py`) hanya dijadikan fallback atau penanda contextual target saat file tersebut aktif di editor.
2. **Polyglot Workspace Support**:
   Modern software repository sering kali bersifat polyglot (misalnya C++ core dengan CMake + Python bindings/scripts + Rust utility CLI). ZDE **tidak boleh** berhenti pada detektor pertama dan membuang bahasa lain. Seluruh ekosistem yang terdeteksi harus diregistrasikan ke dalam target profiles workspace.
3. **Primary vs Contextual Targets**:
   ZDE menentukan satu primary target default berdasarkan prioritas root manifest, tetapi seluruh executable/runnable targets dari bahasa lain tetap tersedia di Toolbar target dropdown. Jika pengguna membuka file tertentu (misal file Python dalam repo C++), file tersebut dapat dieksekusi secara kontekstual tanpa merusak konfigurasi workspace.

---

# 8. Project Detection Priority & Multi-Target Registration

Priority penentuan **Primary Default Target**:

```text
1. CMakeLists.txt              → CMake (C/C++)
2. Cargo.toml                  → Cargo (Rust)
3. pom.xml / build.gradle(.kts)→ Maven / Gradle (Java)
4. Makefile                    → Make
5. Lazarus / Pascal (.lpi/.lpr)→ Free Pascal
6. pyproject.toml / reqs / .py → Python
7. Source file extension       → Contextual Single File Target
```

> [!NOTE]
> Urutan di atas **hanya menentukan default target yang terpilih pertama kali**. Semua target dari sistem lain yang terdeteksi di workspace **tetap didaftarkan** dan dapat dipilih langsung dari Toolbar dropdown.

---

# 9. Execution Models: Compiled Binary vs. Interpreter Script

ZDE membedakan model eksekusi menjadi dua paradigma mendasar:

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. COMPILED BINARY MODEL (C/C++, Rust, Pascal, Go)                          │
├─────────────────────────────────────────────────────────────────────────────┤
│  Manifest (CMakeLists / Cargo.toml)                                         │
│       ↓                                                                     │
│  Build System & Toolchain (cmake --build, cargo build, fpc)                 │
│       ↓                                                                     │
│  Native Binary Artifact Resolution (<build>/bin/<config>/<target>.exe)      │
│       ↓                                                                     │
│  OS Process Runner: Executable = Binary File Langsung                       │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. INTERPRETER SCRIPT MODEL (Python, Node.js, Lua)                          │
├─────────────────────────────────────────────────────────────────────────────┤
│  Project Manifest / Directory Scan (pyproject.toml, requirements.txt, *.py) │
│       ↓                                                                     │
│  Interpreter Resolution (.venv/Scripts/python.exe → system python)          │
│       ↓                                                                     │
│  Project Entry Script Resolution (<project_name>.py / main.py / app.py)     │
│       ↓                                                                     │
│  OS Process Runner: Executable = Python Interpreter, Arg = Entry Script     │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Karakteristik Perbedaan:

| Aspek | Compiled Binary Model | Interpreter Script Model |
|---|---|---|
| **Contoh Bahasa** | C/C++, Rust, Pascal | Python, JavaScript/Node |
| **Output Build** | Native Executable (`.exe`, ELF, Mach-O) | Tidak ada binary native mandiri |
| **Process Program** | File binary hasil kompilasi (`build/.../App.exe`) | Runtime Interpreter (`python.exe` / `.venv`) |
| **Process Target** | Target binary yang dipilih | File script proyek (`server.py`, `main.py`) |
| **Penamaan Proyek vs Target** | Proyek: `ZDE-minimal` → Target Binary: `ZDE`, `ZDEUnitTests` | Proyek: `my_app` → Script: `my_app.py` atau `server.py` |

---

# 10. Project Types & ProjectInfo

```cpp
enum class ExecutionModel {
    CompiledBinary,
    InterpreterScript,
    CustomCommand
};

enum class ProjectType {
    Unknown,

    CMake,
    Cargo,
    Maven,
    Gradle,
    Make,

    Python,
    Pascal,

    PlainCpp,
    PlainJava,
    PlainRust
};

struct RunnableTarget {
    std::string id;
    std::string name;              // Display name (e.g. "ZDE", "my_project (server.py)")
    std::string binaryOrScriptName;// Executable artifact or script file path
    ProjectType projectType;
    ExecutionModel executionModel;
    bool isDefault = false;
};

struct ProjectInfo {
    std::filesystem::path root;
    std::string projectName;       // Manifest-declared or folder name

    ProjectType primaryType;
    std::vector<ProjectType> detectedTypes;
    std::vector<RunnableTarget> targets;

    std::optional<std::filesystem::path> manifest;
    std::optional<std::filesystem::path> virtualEnv; // For Python (.venv, venv)
    bool hasBuildSystem;
};
```

---

# 11. Build System Abstraction

Semua build system harus menggunakan interface yang seragam.

```cpp
class IBuildSystem {
public:
    virtual ~IBuildSystem() = default;

    virtual bool detect(
        const std::filesystem::path& root
    ) = 0;

    virtual BuildResult configure() = 0;

    virtual BuildResult build() = 0;

    virtual std::vector<BuildTarget>
    getTargets() = 0;
};
```

Implementasi:

```text
IBuildSystem
    │
    ├── CMakeBuildSystem
    ├── CargoBuildSystem
    ├── MavenBuildSystem
    ├── GradleBuildSystem
    ├── MakeBuildSystem
    └── FreePascalBuildSystem
```

---

# 12. CMake

CMake menjadi build system reference utama untuk ZDE.

Detection:

```text
CMakeLists.txt
        ↓
CMake Project
```

Jika tersedia:

```text
CMakePresets.json
```

maka ZDE harus memprioritaskan preset.

Workflow:

```text
Detect
  ↓
CMakePresets.json
  ↓
Configure Preset
  ↓
Build Preset
  ↓
Target Discovery (add_executable)
  ↓
Build Target
  ↓
Artifact Resolution (<build>/bin/<config>/<target>.exe)
  ↓
Run Binary
```

Contoh command:

```powershell
cmake --preset <preset>
```

kemudian:

```powershell
cmake --build --preset <preset> --target <target>
```

ZDE tidak boleh hardcode output binary:

```text
build/MyApp.exe
```

Artifact harus diperoleh dari resolusi target name: `<build_dir>/bin/<config>/<target_name>[.exe]`.

---

# 13. CMake Targets: Project Name vs Binary Target Decoupling

Dalam project CMake nyata, **nama project manifest (`project(...)`) sering kali berbeda dari nama binary executable (`add_executable(...)`)**:

Contoh riil:

```cmake
project("ZDE-minimal" LANGUAGES CXX C)

add_executable(ZDE main.cpp)
add_executable(ZDEUnitTests Tests/UnitTests.cpp)
add_executable(Sandbox Sandbox/main.cpp)
```

Di sini:
- **Project Name**: `ZDE-minimal`
- **Binary Target 1**: `ZDE` → Menghasilkan binary `build/.../ZDE.exe`
- **Binary Target 2**: `ZDEUnitTests` → Menghasilkan binary `build/.../ZDEUnitTests.exe`
- **Binary Target 3**: `Sandbox` → Menghasilkan binary `build/.../Sandbox.exe`

### Aturan Resolusi Binary Target ZDE:
1. **Target Discovery Parser**:
   ZDE harus memindai seluruh pernyataan `add_executable\s*\(\s*([A-Za-z0-9_\-]+)` dari `CMakeLists.txt` (termasuk subdirektori seperti `Source/` dan `Tests/`).
2. **Multiple Target Registration**:
   Setiap executable yang ditemukan didaftarkan sebagai `RunnableTarget` terpisah di Toolbar.
3. **Fallback**:
   Jika tidak ada `add_executable` yang ditemukan (misal menggunakan dynamic macro), ZDE fallback ke nama dari `project(...)`.
4. **Artifact Resolution**:
   Path binary di-resolve secara dinamis:
   - Windows: `<workspace>/build/<preset>/bin/<config>/<target>.exe`
   - macOS: `<workspace>/build/<preset>/bin/<config>/<target>.app/Contents/MacOS/<target>` (atau binary langsung)
   - Linux: `<workspace>/build/<preset>/bin/<config>/<target>`

---

# 14. Rust / Cargo: Package vs Binary Targets

Detection:

```text
Cargo.toml
    ↓
Cargo Project
```

Dalam Cargo, project dapat memiliki beberapa binary:
1. Default package binary: `src/main.rs` (nama binary = `[package] name`).
2. Explicit binary sections: `[[bin]] name = "custom_tool" path = "src/bin/tool.rs"`.
3. Standalone binaries dalam folder: `src/bin/*.rs`.

Workflow:

```text
Cargo.toml
    ↓
Cargo Target Discovery (package name + [[bin]] + src/bin/*.rs)
    ↓
cargo build --bin <target>
    ↓
artifact: target/<config>/<target>.exe
    ↓
run: cargo run --bin <target> / direct artifact
```

---

# 15. Java

Java tidak wajib menggunakan Maven. ZDE mendukung tiga mode:
1. Plain Java (`javac` → `java`)
2. Maven (`mvn compile` → `mvn exec:java`)
3. Gradle (`gradle build` → `gradle run`)

Wrapper (`mvnw`, `gradlew`) diprioritaskan apabila tersedia di project root.

---

# 16. Python: Interpreter Model & Project File Alignment

Python dikategorikan sebagai **Interpreter-Based Project**. Karakteristik utamanya:
- Program yang dijalankan OS adalah **Python Interpreter**, bukan binary hasil compile.
- Target yang dijalankan adalah **Project Script / Entry File**.

### 16.1 Project Name vs. Entry Script File Alignment ("Penyelarasan File Proyek")

Nama project Python (dari `pyproject.toml`, `setup.py`, atau folder name) **bisa sama atau berbeda** dengan file script yang dijalankan:

#### Kasus A: Nama File Script Sama dengan Nama Project (Direct Alignment)
Contoh: Project bernama `data_processor`, dan terdapat file `data_processor.py` (atau `data_processor/__main__.py`).
- ZDE mendeteksi kecocokan nama secara otomatis.
- Target Profile: `data_processor` → file `data_processor.py`.
- Run command: `python data_processor.py`.

#### Kasus B: Nama File Script Berbeda dengan Nama Project (Differentiated Naming)
Contoh: Project bernama `zenvra_backend` (dari `pyproject.toml` atau nama folder), tetapi file entry point utamanya adalah `server.py`, `app.py`, `run.py`, `cli.py`, `manage.py`, atau `main.py`.
- ZDE **tidak boleh** memaksakan nama file harus sama dengan nama project.
- ZDE **tidak boleh** mengasumsikan file adalah `main.py` jika `main.py` tidak ada.
- ZDE harus memindai kandidat entry script yang valid:
  1. Entry point manifest: `[project.scripts]` atau `[tool.poetry.scripts]` pada `pyproject.toml`.
  2. File konvensional yang **benar-benar ada** di workspace: `server.py`, `app.py`, `run.py`, `cli.py`, `__main__.py`, `manage.py`, `main.py`.
  3. File `.py` apapun yang berada di workspace root.
- Representasi UI:
  - Jika file adalah `server.py` dalam project `zenvra_backend`:
    `Target: zenvra_backend (server.py)`
    `Executable Path: server.py`
  - Ini memastikan user dan IDE tahu persis file mana yang dieksekusi, meskipun penamaan project dan filenya berbeda!

#### Kasus C: Multi-Script Project
Jika dalam satu project terdapat beberapa script runnable (misalnya `train.py` dan `evaluate.py` dan `server.py`):
- ZDE mendaftarkan masing-masing script sebagai runnable target yang dapat dipilih di Toolbar:
  - `zenvra_backend (server.py)`
  - `zenvra_backend (train.py)`
  - `zenvra_backend (evaluate.py)`

### 16.2 Virtual Environment Discovery Priority
ZDE secara otomatis mengisolasi runtime Python dengan mencari local virtual environment:

```text
1. Project Local Virtualenv:
   - <root>/.venv/Scripts/python.exe (Windows) / <root>/.venv/bin/python (POSIX)
   - <root>/venv/Scripts/python.exe
   - <root>/env/Scripts/python.exe
2. Configured Toolchain Path (Settings)
3. Bundled Python (ZDE/toolchains/python)
4. System PATH (python / python3)
```

Jika `.venv` ditemukan di workspace root, command runner akan langsung menggunakan binary interpreter dari `.venv` tersebut sehingga seluruh package dependencies yang terinstall di virtual environment aktif secara native.


---

# 17. Pascal

Supported extensions:

```text
.pas
.pp
.lpr
.dpr
```

Project markers:

```text
.lpi
.dpr
.lpr
```

Toolchain:

```text
Free Pascal Compiler
fpc
```

Workflow:

```text
Pascal source
      ↓
Free Pascal
      ↓
fpc
      ↓
Executable
      ↓
Run
```

FPC path harus dapat dikonfigurasi melalui Settings.

---

# 18. Toolchain Architecture

Toolchain harus dipisahkan dari Build System.

```cpp
class IToolchain {
public:
    virtual ~IToolchain() = default;

    virtual bool detect() = 0;

    virtual bool isAvailable() const = 0;

    virtual std::string version() const = 0;

    virtual std::filesystem::path executable() const = 0;
};
```

---

# 19. Toolchain Priority

Toolchain resolution:

```text
1. Explicit project configuration
2. Explicit user configuration
3. ZDE bundled toolchain
4. Environment / PATH
5. System default
```

Namun untuk beberapa ecosystem seperti Rust/Java, wrapper atau project-local toolchain dapat memiliki priority lebih tinggi.

---

# 20. Supported Toolchains

## C/C++

```text
LLVM / Clang
MSVC
MinGW / GCC
```

## Rust

```text
rustc
cargo
```

## Java

```text
JDK
Maven
Gradle
```

## Python

```text
Python
```

## Pascal

```text
Free Pascal
```

---

# 21. Bundled Toolchain

ZDE harus memiliki kemampuan menggunakan toolchain yang disediakan bersama distribusi ZDE.

Recommended directory:

```text
ZDE/
├── ZDE.exe
│
├── runtime/
│
├── toolchains/
│   ├── llvm/
│   │   ├── bin/
│   │   ├── lib/
│   │   └── include/
│   │
│   ├── rust/
│   │
│   ├── python/
│   │
│   ├── java/
│   │
│   └── fpc/
│
└── config/
```

Toolchain tidak harus dikompilasi menjadi satu binary `ZDE.exe`.

ZDE hanya menyediakan runtime/toolchain directory yang terisolasi.

---

# 22. Toolchain Settings

Settings:

```text
Settings
│
├── General
├── Editor
├── Terminal
├── Build
│
└── Toolchains
    │
    ├── C/C++
    │   ├── LLVM
    │   ├── MSVC
    │   └── MinGW
    │
    ├── Rust
    │   └── Cargo
    │
    ├── Java
    │   ├── JDK
    │   ├── Maven
    │   └── Gradle
    │
    ├── Python
    │
    └── Pascal
        └── Free Pascal
```

Setiap toolchain harus menyediakan:

```text
Status
Version
Executable Path
Source
```

Contoh:

```text
Free Pascal

Status: Available

Version: 3.x

Compiler:
C:\ZDE\toolchains\fpc\bin\fpc.exe

Source:
Bundled

[Browse]
[Use System]
```

---

# 23. Run Configuration

Run harus berdasarkan configuration, bukan berdasarkan current file saja.

```cpp
struct RunConfiguration {
    std::string name;

    ProjectType projectType;

    std::filesystem::path executable;

    std::filesystem::path workingDirectory;

    std::vector<std::string> arguments;

    std::vector<EnvironmentVariable> environment;

    bool buildBeforeRun;

    std::optional<std::string> buildTarget;
};
```

---

# 24. Automatic Run Configuration

Saat project dibuka:

```text
ProjectDetector
       ↓
BuildSystemDetector
       ↓
ToolchainDetector
       ↓
RunConfigurationResolver
```

Contoh:

```text
CMakeLists.txt
      ↓
CMake
      ↓
Target: ZDE
      ↓
Run Configuration:
"ZDE"
```

Rust:

```text
Cargo.toml
      ↓
Cargo
      ↓
Binary target
      ↓
Run Configuration:
"my-rust-app"
```

Python:

```text
main.py
      ↓
Python
      ↓
Run Configuration:
"main.py"
```

---

# 25. Before Run

Run configuration harus memiliki lifecycle:

```text
Before Run
    ↓
Build
    ↓
Build Success?
    │
    ├── No → Stop
    │
    └── Yes
          ↓
      Resolve Artifact
          ↓
          Run
```

---

# 26. Process Runner

`ProcessRunner` adalah salah satu core service.

```cpp
struct ProcessSpec {
    std::filesystem::path executable;

    std::vector<std::string> arguments;

    std::filesystem::path workingDirectory;

    std::vector<EnvironmentVariable> environment;

    bool redirectStdout;
    bool redirectStderr;
};
```

API:

```cpp
class ProcessRunner {
public:

    ProcessHandle start(
        const ProcessSpec& spec
    );

    ProcessResult run(
        const ProcessSpec& spec
    );

    void terminate(
        ProcessHandle handle
    );
};
```

---

# 27. Platform Implementation

## Windows

Gunakan native process API:

```text
CreateProcessW
```

## Linux

Gunakan:

```text
fork
exec
pipe
waitpid
```

atau `posix_spawn` apabila sesuai.

## macOS

Gunakan:

```text
posix_spawn
```

`system()` tidak boleh digunakan sebagai implementation utama.

---

# 28. Process Events

ProcessRunner harus mengeluarkan event:

```cpp
enum class ProcessEventType {
    Started,
    Stdout,
    Stderr,
    Exit,
    Failed
};
```

Example:

```text
Process Started
       ↓
stdout
       ↓
stdout
       ↓
stderr
       ↓
Process Exit
```

---

# 29. Output Service

Output harus dipisahkan dari ProcessRunner.

```text
ProcessRunner
      ↓
OutputService
      ↓
Build Panel / Terminal
```

Contoh:

```text
Build
────────────────────────────────

[ZDE] Configuring CMake...
[ZDE] Building...

[24%] Building CXX object
[62%] Building CXX object
[100%] Linking executable

Build finished successfully.

Process exited with code 0.
```

---

# 30. Error Handling

Build gagal:

```text
Build Failed
```

harus memberikan:

```text
Command
Exit Code
stdout
stderr
Working Directory
```

Contoh:

```text
Build Failed

Command:
cmake --build --preset windows-debug

Exit Code:
1

Error:
CMake Error ...

[Show Output]
```

---

# 31. Toolbar Requirements

Toolbar minimum:

```text
┌─────────────────────────────────────────────┐
│ Run Configuration ▼    ▶ Run    ■ Stop      │
└─────────────────────────────────────────────┘
```

Run Configuration dropdown:

```text
ZDE
Sandbox
Tests
----------------
Edit Configurations...
```

Jika tidak ada project:

```text
No Run Configuration
```

Jika project belum dikonfigurasi:

```text
Configure Project
```

---

# 32. Run Flow

Ketika user menekan:

```text
▶ Run
```

workflow:

```text
RunService::run()
       ↓
Validate Project
       ↓
Resolve Run Configuration
       ↓
Build Before Run?
       │
       ├── Yes
       │    ↓
       │ BuildService
       │    ↓
       │ Success?
       │
       └── No
            ↓
      Resolve Artifact
            ↓
       ProcessRunner
            ↓
           Run
```

---

# 33. Stop Flow

Ketika user menekan:

```text
■ Stop
```

workflow:

```text
RunService::stop()
        ↓
ProcessManager
        ↓
Active Process Handle
        ↓
Terminate
```

ZDE hanya boleh menghentikan process yang dibuat oleh ZDE.

Jangan melakukan global process kill berdasarkan nama executable.

---

# 34. Build Service

`BuildService` bertanggung jawab terhadap:

- configure
- build
- clean
- target discovery
- build status
- build output

API:

```cpp
class BuildService {
public:

    BuildResult configure(
        const ProjectInfo& project
    );

    BuildResult build(
        const ProjectInfo& project
    );

    BuildResult clean(
        const ProjectInfo& project
    );

    std::vector<BuildTarget>
    targets(
        const ProjectInfo& project
    );
};
```

---

# 35. Artifact Resolver

Artifact resolver bertugas menemukan hasil build.

```cpp
class ArtifactResolver {
public:

    std::optional<std::filesystem::path>
    resolve(
        const BuildResult& result,
        const RunConfiguration& configuration
    );
};
```

Contoh:

```text
Build
 ↓
Target: ZDE
 ↓
ArtifactResolver
 ↓
ZDE.exe
```

Tidak boleh bergantung pada hardcoded:

```text
build/ZDE.exe
```

---

# 36. Execution Provider

Tidak semua project memiliki build system.

Karena itu diperlukan abstraction:

```cpp
class IExecutionProvider {
public:
    virtual ~IExecutionProvider() = default;

    virtual bool canExecute(
        const ProjectInfo&
    ) = 0;

    virtual ProcessSpec createProcess(
        const RunConfiguration&
    ) = 0;
};
```

Implementasi:

```text
IExecutionProvider
    │
    ├── NativeExecutableProvider
    ├── PythonProvider
    ├── JavaProvider
    ├── CargoProvider
    └── FreePascalProvider
```

---

# 37. Separation of Responsibility

## Toolbar

Hanya:

```text
Run
Stop
Select Configuration
```

## RunService

Mengatur:

```text
Run lifecycle
```

## BuildService

Mengatur:

```text
Build lifecycle
```

## ProjectDetector

Mengatur:

```text
Project identification
```

## ToolchainManager

Mengatur:

```text
Compiler/interpreter availability
```

## ProcessRunner

Mengatur:

```text
OS process
```

## ArtifactResolver

Mengatur:

```text
Build output
```

---

# 38. Configuration Storage

Run configuration dapat disimpan di project:

```text
.zde/
├── run/
│   ├── application.json
│   └── tests.json
│
└── project.json
```

atau menggunakan format internal ZDE yang nantinya dapat berubah.

Contoh:

```json
{
    "name": "ZDE",
    "type": "cmake",
    "target": "ZDE",
    "buildBeforeRun": true,
    "arguments": [],
    "workingDirectory": "$PROJECT_ROOT"
}
```

---

# 39. Project Variables

Run Configuration harus mendukung variable:

```text
$PROJECT_ROOT
$PROJECT_DIR
$BUILD_DIR
$TOOLCHAIN_DIR
$EXECUTABLE
```

Contoh:

```text
$PROJECT_ROOT/bin
```

---

# 40. Environment Variables

User dapat menambahkan:

```text
PATH
HOME
ZDE_MODE
CUSTOM_VARIABLE
```

Run Configuration:

```text
Environment

ZDE_MODE=development
CUSTOM_VARIABLE=test
```

---

# 41. Detection Result

Detector tidak hanya mengembalikan boolean.

Contoh:

```cpp
struct DetectionResult {
    bool detected;

    ProjectType type;

    int confidence;

    std::filesystem::path marker;

    std::string reason;
};
```

Contoh:

```text
Detected:

Type:
CMake

Marker:
CMakeLists.txt

Confidence:
100

Reason:
CMakeLists.txt found at project root.
```

---

# 42. Detection Conflict

Project:

```text
Cargo.toml
CMakeLists.txt
```

dapat memiliki dua build system.

ZDE harus:

1. Menentukan primary system berdasarkan priority.
2. Menampilkan detected systems.
3. Memungkinkan user memilih system secara manual.

Contoh:

```text
Detected Project Systems:

● CMake
○ Cargo

[Use CMake]
[Use Cargo]
```

---

# 43. Missing Toolchain

Jika project CMake ditemukan tetapi compiler tidak tersedia:

```text
CMake project detected.

No compatible C++ compiler was found.

[Configure Toolchain]
```

Rust:

```text
Cargo project detected.

Cargo was not found.

[Configure Rust]
```

Pascal:

```text
Pascal project detected.

Free Pascal Compiler was not found.

[Configure FPC]
```

---

# 44. Bundled vs System Toolchain

UI:

```text
Toolchain

○ System
● Bundled

Path:
C:\ZDE\toolchains\llvm\bin\clang.exe

Version:
...
```

User tetap dapat memilih:

```text
Bundled
System
Custom
```

---

# 45. Automatic Toolchain Detection

Toolchain detector harus mencari:

```text
Bundled installation
       ↓
Configured path
       ↓
Project-local path
       ↓
PATH
       ↓
System fallback
```

Detection harus melakukan version check.

Contoh:

```text
clang.exe
    ↓
clang --version
    ↓
valid?
```

---

# 46. Security

ZDE harus berhati-hati terhadap project yang menjalankan executable/script.

Saat membuka project yang belum dipercaya:

```text
This project contains executable build configuration.

Build scripts may execute arbitrary commands.

[Trust Project]
[Open Read-Only]
```

Fitur ini dapat dikembangkan lebih lanjut.

---

# 47. Async Requirement

Build dan Run tidak boleh memblokir UI thread.

Tidak boleh:

```cpp
system("cmake ...");
```

secara synchronous pada UI thread.

Harus:

```text
UI Thread
    │
    └── RunService
            │
            ▼
       Worker Thread
            │
            ▼
       ProcessRunner
```

UI tetap responsif selama:

- configure
- build
- compilation
- execution

---

# 48. Cancellation

Build harus dapat dibatalkan:

```text
Building...
       ↓
User presses Stop
       ↓
Cancel Build
```

Process tree harus diperhatikan, terutama:

```text
CMake
  ↓
Compiler
  ↓
Linker
```

dan:

```text
Maven
  ↓
Java
```

atau:

```text
Cargo
  ↓
rustc
```

Stop harus menangani child process dengan benar.

---

# 49. Initial Supported Matrix

| Language | Project Detection | Build | Run | Toolchain |
|---|---|---|---|---|
| C/C++ | CMake/extension | CMake | Yes | Clang/MSVC/GCC |
| Rust | Cargo.toml | Cargo | Yes | rustc/cargo |
| Java | pom/gradle/extension | Maven/Gradle/javac | Yes | JDK |
| Python | pyproject/.py | N/A | Yes | Python |
| Pascal | .lpi/.dpr/.lpr/.pas | FPC | Yes | FPC |
| Make | Makefile | Make | Yes | System compiler |

---

# 50. Implementation Milestones

## Milestone 1 — ProcessRunner

Implement:

```text
ProcessSpec
ProcessHandle
ProcessResult
stdout
stderr
exit code
terminate
working directory
environment
```

Platform:

```text
Windows
Linux
macOS
```

---

## Milestone 2 — ProjectDetector

Implement:

```text
CMake
Cargo
Maven
Gradle
Make
Python
Pascal
```

---

## Milestone 3 — CMake

Implement first-class CMake support:

```text
CMakeLists.txt
CMakePresets.json
Configure
Build
Target discovery
Artifact resolution
Run
```

ZDE harus bisa build dan menjalankan dirinya sendiri melalui Run Service.

---

## Milestone 4 — RunConfiguration

Implement:

```text
Automatic configuration
Manual configuration
Arguments
Environment
Working directory
Build before run
Target
```

---

## Milestone 5 — Toolbar

Implement:

```text
Run configuration selector
Run
Stop
```

---

## Milestone 6 — ToolchainManager

Implement:

```text
Bundled
System
Custom
```

kemudian:

```text
LLVM
MSVC
MinGW
Rust
JDK
Maven
Python
FPC
```

---

## Milestone 7 — Other Build Systems

Implement:

```text
Cargo
Maven
Gradle
Make
FPC
```

---

# 51. Recommended First Vertical Slice

Jangan mengimplementasikan semua bahasa sekaligus.

Target pertama:

```text
ZDE
 ↓
Open ZDE project
 ↓
Detect CMake
 ↓
Read CMakePresets.json
 ↓
Select preset
 ↓
Configure
 ↓
Build
 ↓
Find ZDE executable
 ↓
Run
 ↓
Show stdout/stderr
 ↓
Stop
```

Jika workflow ini berhasil, architecture sudah terbukti.

Baru kemudian:

```text
CMake
 ↓
Cargo
 ↓
Maven
 ↓
Python
 ↓
FPC
```

---

# 52. Definition of Done

Feature dianggap selesai apabila:

### Project Detection

- [ ] CMake terdeteksi
- [ ] Cargo terdeteksi
- [ ] Maven terdeteksi
- [ ] Gradle terdeteksi
- [ ] Make terdeteksi
- [ ] Python terdeteksi
- [ ] Pascal terdeteksi

### Build

- [ ] Configure berjalan asynchronous
- [ ] Build berjalan asynchronous
- [ ] stdout ditampilkan
- [ ] stderr ditampilkan
- [ ] exit code tersedia
- [ ] build cancellation tersedia

### Run

- [ ] Run Configuration tersedia
- [ ] Build Before Run tersedia
- [ ] Artifact ditemukan otomatis
- [ ] Program berjalan
- [ ] Program dapat dihentikan

### Toolchain

- [ ] Bundled toolchain
- [ ] System toolchain
- [ ] Custom toolchain path
- [ ] Version detection

### UI

- [ ] Run button
- [ ] Stop button
- [ ] Configuration selector
- [ ] Build output panel
- [ ] Toolchain settings

---

# 53. Final Architecture

Target akhir:

```text
                         ZDE
                          │
             ┌────────────┴────────────┐
             │                         │
          Project                   Toolbar
          Service                      │
             │                         │
             ▼                         ▼
       ProjectDetector            RunService
             │                         │
             ▼              ┌──────────┼──────────┐
       ProjectInfo           │          │          │
                            ▼          ▼          ▼
                       RunConfig   BuildService ProcessManager
                                      │             │
                         ┌────────────┼──────┐      │
                         │            │      │      ▼
                       CMake       Cargo   Maven Process
                         │            │      │
                         └────────────┼──────┘
                                      │
                                      ▼
                              ToolchainManager
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
                  LLVM              Rust               JDK
                  MSVC              Cargo              Maven
                  GCC                                 Gradle
                    │
                    ├── Python
                    │
                    └── Free Pascal
                                      │
                                      ▼
                              ArtifactResolver
                                      │
                                      ▼
                               Executable/App
```

---

# 54. Design Principle

ZDE harus mengikuti prinsip:

> **Toolbar should know "what to do", services should know "how to do it".**

Toolbar:

```text
Run
Stop
Select Configuration
```

RunService:

```text
How to execute a project.
```

BuildService:

```text
How to build a project.
```

ProjectDetector:

```text
What kind of project is this?
```

ToolchainManager:

```text
Which compiler/interpreter should be used?
```

ProcessRunner:

```text
How does ZDE execute an OS process?
```

ArtifactResolver:

```text
Where is the result?
```

Dengan pemisahan ini, penambahan bahasa baru tidak akan memaksa perubahan terhadap toolbar atau core RunService.

---

# 55. Long-Term Extension

Architecture harus memungkinkan:

```text
C/C++
Rust
Java
Kotlin
Python
Pascal
C#
Go
Zig
Swift
Fortran
D
Objective-C
HLSL
GLSL
CUDA
```

dan build systems:

```text
CMake
Cargo
Maven
Gradle
Make
Ninja
Meson
MSBuild
Bazel
```

tanpa mengubah core lifecycle:

```text
Detect
  ↓
Configure
  ↓
Build
  ↓
Resolve Artifact
  ↓
Run
```

---

# 56. Success Criteria

ZDE dianggap berhasil mencapai tujuan feature ini ketika developer dapat:

```text
Open Project
      ↓
ZDE automatically detects project
      ↓
ZDE automatically detects build system
      ↓
ZDE finds appropriate toolchain
      ↓
ZDE creates Run Configuration
      ↓
User presses ▶
      ↓
ZDE builds
      ↓
ZDE finds executable
      ↓
ZDE runs application
      ↓
Output appears inside ZDE
      ↓
User presses ■
      ↓
Application stops
```

tanpa developer harus membuka terminal secara manual untuk workflow normal.

**Target pengalaman:**

```text
Open → Detect → Configure → Build → Run
```

bukan:

```text
Open → Figure out compiler → Write command → Open terminal
→ Compile → Search executable → Run manually
```