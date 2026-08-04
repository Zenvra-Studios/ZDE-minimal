# Code Folding Implementation Plan

The user requested that clicking the fold marker (`[-]`) in the gutter should actually hide the code scope, and that the spacing between the line numbers and the fold marker should be improved to look fully cohesive.

Currently, ZDE draws the fold markers but doesn't actually map visual rows to physical document lines. This requires a significant architecture addition to `TextEditor.cpp`.

## Proposed Changes

### 1. View Mapping in `TextEditor.cpp`
To make code folding actually hide lines on screen, we need to separate physical document lines from visual on-screen rows.

#### [MODIFY] `TextEditor.cpp`
- **Scrollbar Synchronization**: Change `m_scrollbar.synchronize(document->get_line_count(), ...)` to synchronize against the total *visible* line count (skipping lines where `m_folding.is_line_hidden()` is true).
- **First Visible Line Calculation**: Before `draw_document` loops over `render_count`, calculate the physical `first_line` by iterating through the document and skipping hidden lines until we reach the visual row index provided by the scrollbar.
- **Drawing Loop**: Modify the Pass 1 and Pass 2 rendering loops to skip physical lines where `m_folding.is_line_hidden()` returns true. Only increment the visual `row` counter when a non-hidden line is drawn.
- **Mouse Interaction**: Update `handle_pointer_press` and `position_from_point` to map the clicked visual row on screen back to the actual physical line in the document, ensuring that clicking on code selects the correct line even when scopes above it are folded.
- **Toggle Fold Render Refresh**: Ensure that when `m_folding.toggle_fold()` is called by clicking the gutter marker, a redraw is correctly scheduled so the text visually collapses immediately.

### 2. Gutter UI and Spacing Improvements
The user noted that the gap between the line numbers and the fold marker looks disjointed. 

#### [MODIFY] `TextEditor.cpp`
- Adjust `number_x` calculation in `draw_document` to bring the line numbers closer to the fold margin.
- Refine the active line background (`active_line_background`) in the gutter so that the fold marker area integrates smoothly with the line number area without awkward gaps.

## Open Questions
None. The implementation is straightforward but touches core text rendering loops.

## Verification Plan
1. **Manual Verification**: 
   - Click a `[-]` marker on a `namespace` or `{` scope. The lines inside should instantly disappear, the marker becomes `[+]`, and the text below should shift up.
   - Click `[+]` to restore the text.
   - Ensure scrolling works flawlessly when massive blocks of code are folded.
   - Ensure clicking text below a folded block places the cursor on the correct line.
2. **Visual UI**: 
   - Verify the line numbers sit neatly next to the fold markers without an awkward gap.
