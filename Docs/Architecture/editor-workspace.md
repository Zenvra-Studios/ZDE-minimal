# Editor Workspace Architecture

The ZDE workspace follows a JetBrains-style composition without coupling the
application to screenshot-specific content. Win32 and X11 expose the same
component boundaries while keeping their native drawing implementations.

```text
UI/Editor/
  EditorFileSystem      - resolve project root, read UTF-8 files, build breadcrumbs
  TextDocumentModel     - mutable lines, caret, editing commands, live status
  StudioEditorModel     - responsive geometry, tabs, sidebar, tokens, palette
                 |
       +---------+---------+
       |                   |
Win32/Components/     X11/Components/
  ActivitySidebar       ActivitySidebar
  FooterToolbar         FooterToolbar
  TextEditor            TextEditor
       |                   |
  StudioWorkspaceRenderer (composition + native drawing resources)
```

`ActivitySidebar` owns the left tool-window rail presentation and its top and
bottom item placement. `FooterToolbar` owns the project breadcrumb and editor
status presentation. `TextEditor` owns the tab strip, gutter, source tokens,
caret line, input mapping, and scrollbar presentation. `StudioWorkspaceRenderer` only
calculates the shared layout, prepares the drawing surface, and composes those
three components.

The separation follows the same broad model/view/controller boundary used by
the VS Code editor core: platform-neutral text state is independent of native
input and rendering. The implementation is ZDE-native C++ and does not embed
Monaco or copy its browser rendering layer.

The Win32 implementation uses GDI and the ClearType branch of
`Utility/Fonts.h`. The X11 implementation uses Xlib and the Xft branch of the
same font helper. Both respect their backend DPI scale.

Each backend opens its own source file through `EditorFileSystem`, with safe
fallback bootstrap text when the source tree is unavailable. The document
supports click-to-caret, text insertion, UTF-8-aware horizontal movement,
arrows, Home/End, Enter, Tab, Backspace, Delete, dynamic line numbers,
caret-following vertical presentation, and dirty state. The footer derives its
breadcrumb, line ending, and live line/column from the active document.

Selection, undo/redo history, file persistence, horizontal scrolling, and
language-service integration remain separate follow-up capabilities.
