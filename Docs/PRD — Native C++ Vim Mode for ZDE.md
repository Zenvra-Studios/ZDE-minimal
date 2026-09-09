# PRD — Native C++ Vim Mode for ZDE

## 1. Overview

Implementasikan **Vim-style editing mode secara native menggunakan C++ di dalam ZDE**.

Implementasi ini harus menjadi bagian dari executable ZDE dan **tidak boleh bergantung pada Vim, Neovim, binary eksternal, plugin Vim, terminal Vim, atau proses editor eksternal lainnya**.

Tujuan utama:

- Menambahkan `Default` dan `Vim` sebagai editor interaction mode.
- Memberikan pengalaman editing bergaya Vim.
- Mempertahankan editor renderer, text buffer, LSP, syntax highlighting, undo/redo, clipboard, search, dan sistem editor ZDE yang sudah ada.
- Vim Mode harus berjalan sepenuhnya di dalam proses ZDE.
- Tidak ada dependency runtime terhadap `vim`, `nvim`, atau executable editor eksternal.
- Implementasi harus modular agar fitur Vim dapat dikembangkan secara bertahap.

---

# 2. Goals

## 2.1 Primary Goals

Implementasikan:

1. Editor interaction mode system.
2. Default editing mode.
3. Native Vim editing mode.
4. Vim state machine.
5. Vim key sequence parser.
6. Vim motions.
7. Vim operators.
8. Vim commands.
9. Insert mode.
10. Normal mode.
11. Visual mode.
12. Search integration.
13. Undo/redo integration.
14. Clipboard integration.
15. Cursor mode/shape integration.
16. Vim mode indicator.
17. Vim-specific settings.
18. Runtime switching antara Default dan Vim.
19. Automated tests untuk Vim behavior.

---

# 3. Non-Goals

Versi awal **tidak perlu** mengimplementasikan seluruh Vim.

Jangan mencoba membuat clone Vim 100%.

Tidak wajib pada initial implementation:

- Vimscript interpreter.
- Lua interpreter.
- Vim plugin ecosystem.
- Neovim API.
- Vim GUI.
- Vim terminal.
- Full Ex command language.
- Full macro system.
- Full register compatibility.
- Remote editing.
- Vim plugin installation.
- `.vimrc` compatibility.
- Neovim compatibility.

Fokus awal adalah:

> **Vim editing behavior di dalam native ZDE editor.**

---

# 4. Critical Architecture Rule

## DO NOT use external Vim

Agent **DILARANG** melakukan pendekatan seperti:

```cpp
system("vim ...");
```

```cpp
system("nvim ...");
```

```cpp
CreateProcess("nvim.exe", ...);
```

atau:

```text
ZDE
 └── Terminal
      └── Vim
```

Implementasi tersebut **bukan bagian dari PRD ini**.

---

# 5. Native Requirement

Vim Mode harus berjalan:

```text
ZDE.exe
│
├── Editor
│
├── Text Buffer
│
├── Renderer
│
├── LSP
│
├── Command System
│
└── Vim Interaction Engine
```

Tidak boleh:

```text
ZDE.exe
│
└── nvim.exe
```

Vim Mode harus menjadi bagian dari executable ZDE.

---

# 6. High-Level Architecture

```text
                         ZDE
                          │
                    Editor Input
                          │
                          ▼
                 EditorInputRouter
                          │
                 ┌────────┴────────┐
                 │                 │
                 ▼                 ▼
            DefaultMode         VimMode
                                   │
                ┌──────────────────┼──────────────────┐
                │                  │                  │
                ▼                  ▼                  ▼
             State             Keymap             Commands
                │                  │                  │
                └──────────────────┼──────────────────┘
                                   │
                                   ▼
                            Editor Command API
                                   │
                                   ▼
                              Editor Engine
                                   │
                     ┌─────────────┼─────────────┐
                     ▼             ▼             ▼
                   Buffer        Cursor       Selection
```

Vim layer harus **mengontrol interaction**, bukan mengambil alih renderer.

---

# 7. Directory Structure

Gunakan struktur yang mengikuti architecture ZDE yang sudah ada.

Recommended:

```text
Source/
├── Editor/
│   ├── Interaction/
│   │   ├── EditorInteractionMode.h
│   │   ├── EditorInteractionMode.cpp
│   │   ├── EditorInputRouter.h
│   │   ├── EditorInputRouter.cpp
│   │   ├── DefaultEditorMode.h
│   │   ├── DefaultEditorMode.cpp
│   │   └── CMakeLists.txt
│   │
│   └── Vim/
│       ├── VimEditorMode.h
│       ├── VimEditorMode.cpp
│       ├── VimState.h
│       ├── VimState.cpp
│       ├── VimMode.h
│       ├── VimKey.h
│       ├── VimKeySequence.h
│       ├── VimKeySequence.cpp
│       ├── VimKeymap.h
│       ├── VimKeymap.cpp
│       ├── VimCommand.h
│       ├── VimCommand.cpp
│       ├── VimCommandRegistry.h
│       ├── VimCommandRegistry.cpp
│       ├── VimMotion.h
│       ├── VimMotion.cpp
│       ├── VimOperator.h
│       ├── VimOperator.cpp
│       ├── VimAction.h
│       ├── VimAction.cpp
│       └── CMakeLists.txt
│
└── ...
```

Jika repository sudah mempunyai struktur editor yang berbeda, **ikuti struktur existing architecture terlebih dahulu**.

Jangan membuat subsystem duplicate hanya karena struktur di atas berbeda.

---

# 8. Editor Interaction Mode

Buat abstraction:

```cpp
class IEditorInteractionMode
{
public:
    virtual ~IEditorInteractionMode() = default;

    virtual void activate() = 0;
    virtual void deactivate() = 0;

    virtual bool handleKey(const EditorKeyEvent& event) = 0;
    virtual bool handleTextInput(const EditorTextInputEvent& event) = 0;
    virtual bool handleMouse(const EditorMouseEvent& event) = 0;

    virtual void update(double deltaTime) = 0;
};
```

Tujuan:

```text
Editor
  │
  ▼
IEditorInteractionMode
  │
  ├── DefaultEditorMode
  │
  └── VimEditorMode
```

Editor tidak boleh mengetahui detail implementasi Vim.

---

# 9. Input Pipeline

Input harus melewati:

```text
Operating System
       │
       ▼
ZDE Window
       │
       ▼
Editor Input Router
       │
       ▼
IEditorInteractionMode
       │
       ├── Default Mode
       │
       └── Vim Mode
              │
              ▼
        Vim Key Processing
              │
              ▼
        Vim Command / Motion
              │
              ▼
        Editor Command API
              │
              ▼
          Text Buffer
```

Vim tidak boleh langsung memanipulasi renderer.

---

# 10. Vim State Machine

Implementasikan state machine.

Initial states:

```text
NORMAL
INSERT
VISUAL
```

Future states dapat ditambahkan:

```text
VISUAL_LINE
VISUAL_BLOCK
REPLACE
COMMAND
OPERATOR_PENDING
```

---

# 11. Vim State

Recommended:

```cpp
enum class VimMode
{
    Normal,
    Insert,
    Visual,
    VisualLine,
    VisualBlock,
    Replace,
    Command,
    OperatorPending
};
```

State harus menyimpan minimal:

```cpp
struct VimState
{
    VimMode mode;

    bool pendingOperator;
    bool pendingCount;

    int count;

    // optional future fields
    bool recordingMacro;
    bool searchActive;
};
```

Jangan memasukkan state Vim ke dalam UI widget.

---

# 12. Mode Transitions

Minimal:

```text
             ESC
     ┌──────────────────┐
     │                  ▼
NORMAL ────────────── INSERT
  │                     ▲
  │                     │
  └────── i/a/o ────────┘
```

Visual:

```text
NORMAL
  │
  └── v ──► VISUAL
              │
              └── ESC ──► NORMAL
```

Basic transitions:

| Input | From | To |
|---|---|---|
| `i` | Normal | Insert |
| `I` | Normal | Insert |
| `a` | Normal | Insert |
| `A` | Normal | Insert |
| `o` | Normal | Insert |
| `O` | Normal | Insert |
| `v` | Normal | Visual |
| `Esc` | Insert | Normal |
| `Esc` | Visual | Normal |

---

# 13. Normal Mode

Normal Mode adalah mode utama Vim.

Minimal support:

### Movement

```text
h
j
k
l
```

### Word movement

```text
w
b
e
```

### Line movement

```text
0
^
$
```

### Document movement

```text
gg
G
```

### Character operations

```text
x
X
```

### Insert

```text
i
I
a
A
o
O
```

### Delete

```text
dd
dw
de
d$
D
```

### Change

```text
cc
cw
ce
c$
C
```

### Yank

```text
yy
yw
y$
```

### Paste

```text
p
P
```

### Undo / Redo

```text
u
Ctrl+r
```

---

# 14. Insert Mode

Insert Mode harus menggunakan editor text insertion pipeline yang sudah tersedia.

Contoh:

```text
NORMAL
   │
   │ i
   ▼
INSERT
   │
   ├── text input
   ├── backspace
   ├── delete
   ├── enter
   └── escape
```

Saat Insert Mode aktif:

```text
keyboard text
      ↓
Editor text insertion
      ↓
Text Buffer
```

Jangan membuat text buffer kedua khusus Vim.

---

# 15. Visual Mode

Minimal:

```text
v
```

memulai character-wise selection.

```text
Esc
```

mengakhiri Visual Mode.

Movement harus memperbesar/mengurangi selection.

Contoh:

```text
v
hjkl
w
b
$
```

Selection harus menggunakan sistem selection ZDE jika sudah tersedia.

Jangan membuat selection model kedua khusus Vim kecuali sistem existing benar-benar tidak mendukung kebutuhan tersebut.

---

# 16. Vim Motions

Buat abstraction motion:

```cpp
class VimMotion
{
public:
    virtual ~VimMotion() = default;

    virtual CursorPosition execute(
        const EditorContext& context,
        const CursorPosition& cursor
    ) = 0;
};
```

Minimal motions:

```text
h
j
k
l

w
b
e

0
^
$

gg
G
```

Future:

```text
f<char>
F<char>
t<char>
T<char>

%
{
}

(
)

ge
W
B
E
```

---

# 17. Operators

Vim menggunakan konsep:

```text
Operator + Motion
```

Contoh:

```text
d + w = dw
c + w = cw
y + w = yw
```

Buat abstraction:

```cpp
class VimOperator
{
public:
    virtual ~VimOperator() = default;

    virtual void execute(
        EditorContext& context,
        const TextRange& range
    ) = 0;
};
```

Minimal operators:

```text
d = delete
c = change
y = yank
```

Future:

```text
> = indent
< = unindent
g~ = case change
gu = lowercase
gU = uppercase
```

---

# 18. Operator Pending State

Contoh:

```text
d
```

tidak langsung melakukan delete.

State berubah:

```text
NORMAL
   │
   │ d
   ▼
OPERATOR_PENDING
   │
   │ w
   ▼
execute delete(word)
   │
   ▼
NORMAL
```

Implementasikan sequence:

```text
d + w
d + $
d + d

c + w
c + $
c + c

y + w
y + $
y + y
```

---

# 19. Key Sequence Parser

Vim tidak selalu menggunakan satu key = satu action.

Contoh:

```text
gg
dd
dw
cw
yy
```

Karena itu implementasikan key sequence handling.

Contoh:

```cpp
class VimKeySequence
{
public:
    void push(VimKey key);
    void clear();

    bool isComplete() const;
    bool isPartial() const;
};
```

Flow:

```text
Key
 ↓
Keymap
 ↓
Sequence Parser
 ↓
Complete?
 ├── No → wait for next key
 └── Yes → execute command
```

---

# 20. Numeric Counts

Support numeric prefix.

Contoh:

```text
5j
3w
2dd
10l
```

Parser harus memahami:

```text
count + command
```

Contoh:

```text
5j
```

berarti:

```text
moveDown(5)
```

dan:

```text
3dd
```

berarti:

```text
deleteLine(3)
```

---

# 21. Vim Command Registry

Gunakan registry agar command tidak hardcoded seluruhnya dalam `VimEditorMode`.

Contoh:

```cpp
class VimCommandRegistry
{
public:
    void registerCommand(
        VimCommandId id,
        VimCommand command
    );

    VimCommand* find(VimCommandId id);
};
```

Concept:

```text
Vim Key
   ↓
Keymap
   ↓
Command ID
   ↓
Command Registry
   ↓
Command
   ↓
Editor API
```

---

# 22. Vim Keymap

Pisahkan keymap dari command implementation.

Contoh:

```text
"h" → MoveLeft
"j" → MoveDown
"k" → MoveUp
"l" → MoveRight

"w" → MoveWordForward
"b" → MoveWordBackward

"i" → EnterInsert
"v" → EnterVisual

"x" → DeleteCharacter
```

Operator sequence:

```text
"d" → DeleteOperator
"c" → ChangeOperator
"y" → YankOperator
```

---

# 23. Editor Command Integration

Vim layer harus menggunakan command API ZDE.

Contoh:

```text
VimMotion
     ↓
Editor.moveCursor()

VimDelete
     ↓
Editor.deleteRange()

VimYank
     ↓
Editor.copyRange()

VimPaste
     ↓
Editor.insertText()

VimUndo
     ↓
Editor.undo()
```

Jangan membuat implementasi kedua untuk:

- text deletion
- insertion
- undo
- clipboard
- cursor storage
- selection storage

jika subsystem tersebut sudah tersedia.

---

# 24. Undo / Redo

Gunakan undo system existing.

Mapping:

```text
u
```

→

```cpp
editor.undo();
```

dan:

```text
Ctrl+r
```

→

```cpp
editor.redo();
```

Vim layer hanya menerjemahkan input.

---

# 25. Clipboard

Minimal:

```text
yy
yw
y$
```

harus dapat melakukan yank.

```text
p
P
```

harus paste.

Prioritaskan penggunaan clipboard abstraction ZDE.

Jangan membuat clipboard implementation sendiri jika platform abstraction sudah tersedia.

---

# 26. Search

Integrasikan Vim search dengan search subsystem ZDE jika tersedia.

Minimal:

```text
/
?
n
N
```

Flow:

```text
/
 ↓
Search Mode
 ↓
Search Input
 ↓
Existing Search Engine
```

Jangan membuat search engine kedua hanya untuk Vim.

---

# 27. Cursor Behavior

Cursor behavior harus berubah berdasarkan Vim mode.

Contoh:

```text
Normal Mode
    → block cursor

Insert Mode
    → line/bar cursor

Visual Mode
    → selection-aware cursor
```

Cursor rendering tetap menjadi tanggung jawab editor renderer.

Vim hanya memberikan state:

```cpp
VimMode mode();
```

Renderer menentukan bentuk visual.

---

# 28. Mode Indicator

Editor harus menampilkan mode aktif.

Contoh:

```text
NORMAL
INSERT
VISUAL
```

Bisa ditampilkan di status bar/editor chrome.

Jangan membuat renderer khusus Vim.

Gunakan existing status bar/component architecture.

---

# 29. Vim Settings

Tambahkan:

```text
editor.interactionMode
```

Values:

```text
default
vim
```

Vim-specific settings:

```text
vim.enabled
vim.startMode
vim.leaderKey
vim.escapeKey
vim.relativeLineNumbers
vim.showModeIndicator
vim.cursorShape
vim.timeout
```

Recommended defaults:

```json
{
    "editor.interactionMode": "default",

    "vim.enabled": false,
    "vim.startMode": "normal",
    "vim.leaderKey": "\\",
    "vim.escapeKey": "Escape",
    "vim.relativeLineNumbers": false,
    "vim.showModeIndicator": true,
    "vim.timeout": 1000
}
```

---

# 30. Default Mode

Default Mode harus tetap menjadi behavior editor ZDE saat ini.

Jangan mengubah behavior Default Mode hanya untuk menambahkan Vim.

```text
editor.interactionMode = default
```

→

```text
DefaultEditorMode
```

---

# 31. Vim Mode Activation

Saat:

```text
editor.interactionMode = vim
```

maka:

```text
EditorInputRouter
        ↓
VimEditorMode
```

Saat:

```text
editor.interactionMode = default
```

maka:

```text
EditorInputRouter
        ↓
DefaultEditorMode
```

---

# 32. Runtime Switching

Mode harus dapat diubah melalui Settings tanpa restart jika architecture ZDE memungkinkan.

Flow:

```text
SettingsService
      │
      ▼
editor.interactionMode
      │
      ▼
SettingsChangedEvent
      │
      ▼
EditorInteractionController
      │
      ├── DefaultEditorMode
      │
      └── VimEditorMode
```

Ketika berpindah dari Vim ke Default:

```text
cancel pending sequence
cancel operator pending
reset Vim state
```

Ketika berpindah dari Default ke Vim:

```text
initialize Vim state
mode = Normal
clear pending sequence
```

---

# 33. Input Conflict

Vim input harus memiliki priority hanya ketika Vim Mode aktif.

Contoh:

```text
Default Mode:
j → insert / normal editor behavior

Vim Mode:
j → move cursor down
```

Jangan mengubah global keyboard handling.

---

# 34. Mouse Support

Initial implementation boleh menggunakan existing mouse behavior.

Minimal:

- mouse click
- cursor placement
- text selection
- drag selection

Future:

- Vim mouse modes
- visual selection interaction
- multi-cursor compatibility

Jangan memblokir native editor mouse interaction tanpa alasan.

---

# 35. Multi-Cursor

Multi-cursor tidak wajib pada fase pertama.

Namun architecture harus tidak mengunci Vim implementation sehingga multi-cursor tidak mungkin ditambahkan.

---

# 36. Macros

Macro support adalah future feature.

Architecture dapat menyediakan:

```cpp
class VimMacroRecorder;
```

tetapi tidak perlu mengimplementasikannya pada initial milestone.

Future commands:

```text
q<char>
@<char>
@@
```

---

# 37. Registers

Initial implementation cukup memiliki internal yank/paste abstraction.

Future:

```text
"
0
"
a
"
b
```

dan unnamed register.

Jangan mencoba implementasi seluruh register Vim pada fase pertama.

---

# 38. Ex Commands

Tidak wajib pada initial release.

Future:

```text
:
:w
:q
:wq
:e
:%s/foo/bar/g
```

Jika nantinya dibuat, Ex Command Engine harus menjadi subsystem terpisah:

```text
Source/Editor/Vim/Ex/
```

Jangan memasukkan parser Ex ke `VimEditorMode`.

---

# 39. Search Command Mode

Future:

```text
/
?
:
```

dapat menggunakan command input overlay yang sama.

Jika ZDE sudah memiliki prompt/input component, gunakan component tersebut.

---

# 40. Leader Key

Leader key dapat disediakan untuk future custom mappings.

Contoh:

```text
vim.leaderKey = "\\"
```

Jangan mengimplementasikan plugin-style mapping system pada initial release.

---

# 41. Key Timeout

Sequence seperti:

```text
g
```

harus dapat menunggu input berikutnya untuk:

```text
gg
```

Gunakan:

```text
vim.timeout
```

Default:

```text
1000 ms
```

Jangan membuat blocking sleep pada UI thread.

Gunakan timestamp/state machine.

---

# 42. Threading

Vim input processing harus berjalan pada thread/context editor yang sudah digunakan ZDE.

Jangan membuat thread baru untuk setiap key.

Sequence timeout harus non-blocking.

Contoh:

```cpp
update(deltaTime)
```

memeriksa timeout.

---

# 43. Performance

Vim interaction layer harus lightweight.

Target:

- Tidak melakukan allocation besar untuk setiap keypress.
- Tidak membaca settings dari disk pada setiap input.
- Tidak melakukan file I/O saat keypress.
- Tidak membuat process eksternal.
- Tidak menghambat renderer.
- Tidak menghambat LSP.
- Tidak membuat text buffer duplicate.

---

# 44. Error Handling

Jika Vim command invalid:

```text
input
 ↓
Vim parser
 ↓
invalid
 ↓
reset sequence
```

Editor tidak boleh crash.

Jika sequence timeout:

```text
pending sequence
 ↓
timeout
 ↓
clear sequence
 ↓
return to Normal
```

---

# 45. Testing

Buat automated tests untuk behavior Vim.

Recommended:

```text
Tests/
└── Editor/
    └── Vim/
        ├── VimStateTests.cpp
        ├── VimKeymapTests.cpp
        ├── VimMotionTests.cpp
        ├── VimOperatorTests.cpp
        ├── VimCommandTests.cpp
        └── VimSequenceTests.cpp
```

---

# 46. Required Tests

## Movement

```text
h
j
k
l
w
b
e
0
^
$
gg
G
```

## Insert

```text
i
I
a
A
o
O
Esc
```

## Delete

```text
x
dd
dw
de
D
```

## Change

```text
cw
cc
C
```

## Yank/Paste

```text
yy
yw
p
P
```

## Undo/Redo

```text
u
Ctrl+r
```

## Visual

```text
v
Esc
```

## Counts

```text
5j
3w
2dd
```

## Sequences

```text
gg
dd
dw
cw
yy
```

---

# 47. Test Strategy

Test behavior menggunakan:

```text
Initial Text
      ↓
Vim Input Sequence
      ↓
Editor State
      ↓
Expected Text/Cursor/Selection
```

Contoh:

```text
Input:

hello world

Keys:

dw

Expected:

world
```

Contoh:

```text
Input:

hello
world

Keys:

gg
j
```

Expected cursor:

```text
line 2
```

---

# 48. Architecture Boundaries

## Vim boleh bergantung kepada:

```text
Core
Editor
Input
Commands
Text Buffer
Selection
Cursor
Undo
Clipboard
Search
Settings
Events
```

## Vim tidak boleh bergantung langsung kepada:

```text
Renderer implementation
Win32
Cocoa
X11
UI widget implementation
```

Gunakan abstraction.

---

# 49. Platform Independence

Vim implementation harus platform-independent.

Tidak boleh ada:

```cpp
#ifdef _WIN32
    // Vim command
#endif
```

untuk Vim behavior.

Platform input harus diterjemahkan menjadi common ZDE input events terlebih dahulu.

```text
Win32 Input
Linux Input
macOS Input
     │
     ▼
EditorKeyEvent
     │
     ▼
Vim Engine
```

---

# 50. AI Agent Implementation Rules

AI agent **WAJIB** mengikuti aturan berikut.

### Rule 1 — Inspect first

Sebelum menulis code:

- inspect repository;
- cari existing editor implementation;
- cari input abstraction;
- cari command system;
- cari cursor system;
- cari selection system;
- cari undo/redo;
- cari clipboard;
- cari search;
- cari settings;
- cari event system;
- cari existing tests.

Jangan langsung membuat architecture baru.

---

### Rule 2 — Reuse existing systems

Jika ZDE sudah mempunyai:

```text
EditorCommand
TextBuffer
Cursor
Selection
UndoManager
Clipboard
Search
SettingsService
EventBus
```

gunakan subsystem tersebut.

Jangan membuat duplicate.

---

### Rule 3 — No external Vim

Dilarang:

```text
vim executable
nvim executable
libvim runtime
embedded terminal Vim
external Vim process
```

Implementasi harus pure native C++.

---

### Rule 4 — No WebView

Vim Mode tidak boleh menggunakan:

```text
WebView
JavaScript editor
Monaco
CodeMirror
xterm
```

Vim interaction harus native C++.

---

### Rule 5 — No giant rewrite

Jangan melakukan rewrite editor hanya demi Vim Mode.

Integrasikan Vim ke architecture existing.

---

### Rule 6 — Incremental implementation

Implementasikan secara bertahap:

```text
Phase 1
Interaction abstraction

Phase 2
Vim state machine

Phase 3
Normal mode movement

Phase 4
Insert mode

Phase 5
Operators

Phase 6
Visual mode

Phase 7
Undo/redo + clipboard

Phase 8
Search

Phase 9
Settings integration

Phase 10
Tests + polish
```

---

# 51. Implementation Phases

## Phase 1 — Editor Interaction Layer

Implement:

```text
IEditorInteractionMode
EditorInputRouter
DefaultEditorMode
```

Pastikan existing editor behavior tidak berubah.

---

## Phase 2 — Vim State

Implement:

```text
VimEditorMode
VimState
VimMode
```

Support:

```text
Normal
Insert
Visual
```

---

## Phase 3 — Basic Navigation

Implement:

```text
h
j
k
l

w
b
e

0
^
$

gg
G
```

---

## Phase 4 — Insert

Implement:

```text
i
I
a
A
o
O
Esc
```

Gunakan existing text insertion system.

---

## Phase 5 — Operators

Implement:

```text
d
c
y
```

dan:

```text
dd
dw
de
d$

cw
cc
c$

yy
yw
y$
```

---

## Phase 6 — Visual

Implement:

```text
v
Esc
```

dan movement dalam Visual Mode.

---

## Phase 7 — Clipboard / Undo

Integrasikan:

```text
u
Ctrl+r
p
P
```

---

## Phase 8 — Search

Implement:

```text
/
?
n
N
```

dengan existing search engine.

---

## Phase 9 — Settings

Implement:

```text
editor.interactionMode
```

dan Vim settings.

---

## Phase 10 — Polish

Tambahkan:

- mode indicator;
- cursor shape;
- timeout;
- relative line numbers;
- key sequence feedback;
- error handling;
- performance optimization;
- comprehensive tests.

---

# 52. Future Roadmap

Setelah initial Vim Mode stabil:

### Stage 2

```text
f
F
t
T
%
{
}
(
)
```

### Stage 3

```text
Visual Line
Visual Block
Replace Mode
```

### Stage 4

```text
Registers
Macros
Marks
```

### Stage 5

```text
Ex Commands
Command Mode
```

### Stage 6

```text
Vim configuration
Custom mappings
```

### Stage 7

```text
Advanced Vim compatibility
```

---

# 53. Acceptance Criteria

Implementasi dianggap berhasil apabila:

- [ ] ZDE dapat memilih Default Mode.
- [ ] ZDE dapat memilih Vim Mode.
- [ ] Vim Mode berjalan tanpa binary Vim/Neovim.
- [ ] Tidak ada process editor eksternal.
- [ ] Normal Mode bekerja.
- [ ] Insert Mode bekerja.
- [ ] Visual Mode bekerja.
- [ ] Basic motions bekerja.
- [ ] Basic operators bekerja.
- [ ] Counts bekerja.
- [ ] Yank/paste bekerja.
- [ ] Undo/redo bekerja.
- [ ] Search bekerja.
- [ ] Cursor berubah sesuai mode.
- [ ] Mode indicator bekerja.
- [ ] Settings dapat mengubah interaction mode.
- [ ] Editor existing tidak rusak ketika Default Mode aktif.
- [ ] Vim implementation tidak bergantung kepada renderer.
- [ ] Vim implementation tidak bergantung kepada platform API.
- [ ] Automated tests tersedia.
- [ ] Tidak ada dependency runtime terhadap Vim/Neovim.

---

# 54. Definition of Done

Vim Mode dianggap production-ready untuk initial release ketika:

```text
ZDE.exe
   │
   ├── Native Editor
   │
   ├── Native Renderer
   │
   ├── Native LSP
   │
   ├── Native Settings
   │
   └── Native Vim Engine
```

dan seluruh Vim interaction berjalan **100% di dalam process ZDE**.

User harus dapat melakukan:

```text
Settings
   ↓
Editor
   ↓
Interaction Mode
   ↓
Vim
```

kemudian langsung menggunakan:

```text
h j k l
w b e
gg G
i I a A o O
x dd dw D
cw cc C
yy yw
p P
u Ctrl+r
v
/ ?
```

tanpa menginstall atau menjalankan Vim/Neovim secara terpisah.

---

# 55. Final Architecture

```text
                         ┌──────────────────────┐
                         │         ZDE          │
                         │                      │
                         │  Native C++ Editor   │
                         └──────────┬───────────┘
                                    │
                                    ▼
                         ┌──────────────────────┐
                         │ EditorInputRouter    │
                         └──────────┬───────────┘
                                    │
                     ┌──────────────┴──────────────┐
                     │                             │
                     ▼                             ▼
              DefaultEditorMode             VimEditorMode
                                                   │
                          ┌────────────────────────┼───────────────────┐
                          │                        │                   │
                          ▼                        ▼                   ▼
                     VimState                 VimKeymap          VimCommand
                          │                        │                   │
                          ▼                        ▼                   ▼
                    VimMode Parser          Key Sequence       Command Registry
                          │                        │                   │
                          └────────────────────────┼───────────────────┘
                                                   │
                                                   ▼
                                          ZDE Editor Command API
                                                   │
                         ┌─────────────────────────┼──────────────────────┐
                         │                         │                      │
                         ▼                         ▼                      ▼
                    Text Buffer                Cursor                 Selection
                         │                         │                      │
                         └─────────────────────────┼──────────────────────┘
                                                   │
                                                   ▼
                                               Renderer
```

**Core principle:**

> **Vim adalah interaction layer, bukan editor kedua.**

> **Vim behavior ditulis native dalam C++.**

> **Text buffer, renderer, LSP, undo, clipboard, search, dan UI tetap milik ZDE.**

> **Tidak ada Vim/Neovim binary atau process eksternal.**