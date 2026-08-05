#include "Platform/X11/Components/TextEditor.h"
#include "Commands/CommandIds.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace Zenvra::Platform::X11::Components {

namespace {

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

bool is_utf8_continuation(char character) {
  return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
}

std::size_t next_character_column(std::string_view line, std::size_t column) {
  column = std::min(column + 1, line.size());
  while (column < line.size() && is_utf8_continuation(line[column])) {
    ++column;
  }
  return column;
}

bool has_gutter_marker(std::string_view line) {
  return line.find("namespace ") != std::string_view::npos ||
         (line.find("::") != std::string_view::npos &&
          line.find('(') != std::string_view::npos);
}

std::size_t visual_row_to_physical_line(const UI::Components::EditorFoldingModel &folding,
                                        std::size_t visual_row,
                                        std::size_t total_lines) {
  std::size_t current_visual = 0;
  for (std::size_t i = 0; i < total_lines; ++i) {
    if (!folding.is_line_hidden(i)) {
      if (current_visual == visual_row) return i;
      current_visual++;
    }
  }
  return total_lines > 0 ? total_lines - 1 : 0;
}

std::size_t physical_line_to_visual_row(const UI::Components::EditorFoldingModel &folding,
                                        std::size_t physical_line,
                                        std::size_t total_lines) {
  std::size_t visual_row = 0;
  for (std::size_t i = 0; i < physical_line && i < total_lines; ++i) {
    if (!folding.is_line_hidden(i)) {
      visual_row++;
    }
  }
  return visual_row;
}

std::size_t count_visible_lines(const UI::Components::EditorFoldingModel &folding,
                                std::size_t total_lines) {
  std::size_t visible = 0;
  for (std::size_t i = 0; i < total_lines; ++i) {
    if (!folding.is_line_hidden(i)) visible++;
  }
  return visible;
}

/// Returns the physical line of a foldable marker under the pointer, or
/// std::nullopt when the pointer is not over a foldable fold widget.
std::optional<std::size_t>
fold_start_line_at_point(const UI::Components::EditorFoldingModel &folding,
                         const UI::Editor::StudioEditorLayoutResult &layout,
                         float point_x, float point_y, float dpi_scale,
                         std::size_t first_visual_row,
                         std::size_t total_lines) {
  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * dpi_scale;
  const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
  if (!layout.gutter_bounds.contains(point_x, point_y) ||
      point_x < fold_margin_left) {
    return std::nullopt;
  }
  const float line_height = 20.0F * dpi_scale;
  const std::size_t clicked_row = static_cast<std::size_t>(std::max(
      static_cast<int>((point_y - layout.editor_bounds.y) / line_height), 0));
  const std::size_t line_index = visual_row_to_physical_line(
      folding, first_visual_row + clicked_row, total_lines);
  if (!folding.is_fold_start(line_index)) {
    return std::nullopt;
  }
  return line_index;
}

std::pair<std::optional<UI::Editor::TextPosition>,
          std::optional<UI::Editor::TextPosition>>
find_enclosing_braces(const UI::Editor::TextDocumentModel &document) {
  const std::size_t start_line = document.get_caret_line();
  const std::size_t start_col = document.get_caret_column();

  std::optional<UI::Editor::TextPosition> open_brace;
  std::optional<UI::Editor::TextPosition> close_brace;

  // Simplistic backward search for unmatched '{'
  int brace_depth = 0;
  bool found_open = false;

  // Limit backward search to ~500 lines to avoid freezing on massive files
  // without braces
  const int search_limit = std::max(0, static_cast<int>(start_line) - 500);

  for (int line_idx = static_cast<int>(start_line); line_idx >= search_limit;
       --line_idx) {
    std::string_view line =
        document.get_line(static_cast<std::size_t>(line_idx));

    // Start searching from the character AT the cursor (start_col) or the end
    // of the line
    int search_end = static_cast<int>(line.size());
    if (line_idx == static_cast<int>(start_line)) {
      search_end = std::min(static_cast<int>(start_col) + 1, search_end);
    }

    for (int i = search_end - 1; i >= 0; --i) {
      if (line[i] == '}') {
        ++brace_depth;
      } else if (line[i] == '{') {
        if (brace_depth > 0) {
          --brace_depth;
        } else {
          open_brace = UI::Editor::TextPosition{
              static_cast<std::size_t>(line_idx), static_cast<std::size_t>(i)};
          found_open = true;
          break;
        }
      }
    }
    if (found_open)
      break;
  }

  // Simplistic forward search for matching '}' starting from the open brace
  if (found_open) {
    brace_depth = 0;
    bool found_close = false;
    const std::size_t doc_lines = document.get_line_count();
    const std::size_t forward_limit =
        std::min(doc_lines, open_brace->line + 1500);

    for (std::size_t line_idx = open_brace->line; line_idx < forward_limit;
         ++line_idx) {
      std::string_view line = document.get_line(line_idx);
      std::size_t search_start =
          (line_idx == open_brace->line) ? open_brace->column : 0;

      for (std::size_t i = search_start; i < line.size(); ++i) {
        if (line[i] == '{') {
          ++brace_depth;
        } else if (line[i] == '}') {
          --brace_depth;
          if (brace_depth == 0) {
            close_brace = UI::Editor::TextPosition{line_idx, i};
            found_close = true;
            break;
          }
        }
      }
      if (found_close)
        break;
    }
  }

  return {open_brace, close_brace};
}

} // namespace

bool TextEditor::open_file(const std::filesystem::path &path) {
  const bool opened = m_controller.open_file(path);
  if (opened) {
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
  }
  return opened;
}

std::size_t TextEditor::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths) {
  const std::size_t opened_count =
      m_controller.open_dropped_paths(dropped_paths);
  if (opened_count > 0) {
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_focused = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
    m_hovered_fold_line.reset();
  }
  return opened_count;
}

bool TextEditor::create_buffer() {
  const bool created = m_controller.create_buffer();
  if (created) {
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_focused = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
    m_hovered_fold_line.reset();
  }
  return created;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  if (!layout.tab_bar_bounds.contains(point_x, point_y)) {
    return false;
  }

  float tab_x = layout.tab_bar_bounds.x;
  const float right_limit = layout.tab_bar_bounds.right();
  const std::span<const UI::Editor::EditorSessionDocument> documents =
      m_controller.get_documents();
  for (const UI::Editor::EditorSessionDocument &document : documents) {
    const float width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{document.text.get_file_name()})),
        surface.m_dpi_scale);
    if (tab_x + width > right_limit) {
      break;
    }
    const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                          layout.tab_bar_bounds.height};
    if (bounds.contains(point_x, point_y)) {
      return true;
    }
    tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                         surface.m_dpi_scale;
  }
  return false;
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, bool extend_selection, int click_count,
    std::string &command_out) {
  if (layout.tab_bar_bounds.contains(point_x, point_y)) {
    float tab_x = layout.tab_bar_bounds.x;
    const float right_limit = layout.tab_bar_bounds.right();
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (std::size_t index = 0; index < documents.size(); ++index) {
      const float width = UI::Editor::calculate_editor_tab_width(
          static_cast<float>(surface.m_ui_font->getTextWidth(
              std::string{documents[index].text.get_file_name()})),
          surface.m_dpi_scale);
      if (tab_x + width > right_limit) {
        break;
      }
      const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                            layout.tab_bar_bounds.height};
      if (bounds.contains(point_x, point_y)) {
        const UI::Rect close_bounds{
            bounds.right() -
                UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                    surface.m_dpi_scale,
            bounds.y,
            UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                surface.m_dpi_scale,
            bounds.height};
        m_focused = true;
        if (close_bounds.contains(point_x, point_y)) {
          const bool closed = m_controller.close_file(index);
          if (closed) {
            m_scrollbar.reset();
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            m_hovered_tab_index.reset();
            m_hovered_tab_close_index.reset();
            m_hovered_fold_line.reset();
          }
          return closed;
        }
        if (m_controller.activate_file(index)) {
          m_scrollbar.reset();
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
        }
        m_tab_drag_drop.begin_drag(index, point_x);
        m_drag_initial_tab_x = bounds.x;
        return true;
      }
      tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                           surface.m_dpi_scale;
    }
    // Only consume the titlebar when a real tab/action was hit. Empty
    // space is intentionally left for the native window drag region.
    return false;
  }

  UI::Editor::TextDocumentModel *document = m_controller.get_active_document();
  if (document != nullptr && m_minimap.is_point(layout, point_x, point_y)) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
    const std::optional<std::size_t> target = m_minimap.handle_pointer_press(
        layout, point_x, point_y, count_visible_lines(m_folding, total_lines),
        visible_count, m_scrollbar.get_first_visible_line());
    if (target) {
      static_cast<void>(m_scrollbar.scroll_to(*target));
    }
    m_focused = true;
    m_pointer_selecting = false;
    m_reveal_caret_pending = false;
    m_caret_blink.reset();
    return true;
  }
  if (document != nullptr && m_scrollbar.is_point(layout, point_x, point_y)) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(count_visible_lines(m_folding,
                                                document->get_line_count()),
                            visible_count);
    m_focused = true;
    m_pointer_selecting = false;
    m_reveal_caret_pending = false;
    m_caret_blink.reset();
    return m_scrollbar.handle_pointer_press(layout, point_x, point_y);
  }

  if (document == nullptr) {
    if (layout.editor_bounds.contains(point_x, point_y)) {
      const float dpi = surface.m_dpi_scale;
      const int center_x = round_to_int(layout.editor_bounds.x +
                                        layout.editor_bounds.width * 0.5F);
      const int logo_size = round_to_int(150.0F * dpi);
      const int gap1 = round_to_int(30.0F * dpi);
      const int gap2 = round_to_int(40.0F * dpi);
      const int total_height =
          logo_size + gap1 + gap2 + 3 * round_to_int(28.0F * dpi);
      const float full_height = layout.editor_bounds.height + layout.terminal_panel_bounds.height;
      int current_y =
          round_to_int(layout.editor_bounds.y +
                       (full_height - total_height) * 0.5F);
      current_y += logo_size + gap1;
      current_y += gap2;

      const float btn_w = 300.0F * dpi;
      const float btn_h = 40.0F * dpi;
      const float btn_x = center_x - btn_w * 0.5F;

      m_empty_state_open_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);
      m_empty_state_clone_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});

      if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y)) {
        command_out = "zde.project.open";
        return true;
      }
      if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
        return true;
      }
    }
    return false;
  }

  if (!layout.gutter_bounds.contains(point_x, point_y) &&
      !layout.editor_bounds.contains(point_x, point_y)) {
    return false;
  }

  // Check if the click is in the fold margin (rightmost part of gutter).
  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * surface.m_dpi_scale;
  const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
  if (layout.gutter_bounds.contains(point_x, point_y) &&
      point_x >= fold_margin_left) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((point_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = visual_row_to_physical_line(
        m_folding, m_scrollbar.get_first_visible_line() + clicked_row,
        total_lines);
    if (m_folding.is_fold_start(line_index)) {
      m_folding.toggle_fold(line_index);
      m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                              visible_count);
      m_reveal_caret_pending = true;
      return true;
    }
  }

  m_focused = true;
  m_pointer_selecting = true;
  const UI::Editor::TextPosition position =
      position_from_point(surface, layout, point_x, point_y);
  const bool gutter_click = layout.gutter_bounds.contains(point_x, point_y);
  bool selected = false;
  if (click_count >= 3 || (click_count >= 2 && gutter_click))
  {
    selected = document->select_line_at(position.line);
  }
  else if (click_count == 2)
  {
    selected = document->select_word_at(position.line, position.column);
  }
  if (!selected)
  {
    static_cast<void>(
        document->set_caret(position.line, position.column, extend_selection));
  }
  m_reveal_caret_pending = true;
  m_caret_blink.reset();
  return true;
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  const bool scrollbar_changed =
      m_scrollbar.set_hovered(layout, point_x, point_y);

  UI::Editor::TextDocumentModel *document = m_controller.get_active_document();
  if (document == nullptr) {
    bool changed = false;
    changed |= m_empty_state_open_btn.handle_pointer_move(point_x, point_y);
    changed |= m_empty_state_clone_btn.handle_pointer_move(point_x, point_y);
    if (changed)
      return true;
  }
  std::optional<std::size_t> hovered_fold_line;
  if (document != nullptr) {
    const std::size_t total_lines = document->get_line_count();
    const float line_height = 20.0F * layout.dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
    hovered_fold_line = fold_start_line_at_point(
        m_folding, layout, point_x, point_y, layout.dpi_scale,
        m_scrollbar.get_first_visible_line(), total_lines);
  }
  if (hovered_fold_line != m_hovered_fold_line) {
    m_hovered_fold_line = hovered_fold_line;
    return true;
  }
  std::optional<std::size_t> hovered_tab;
  std::optional<std::size_t> hovered_close;
  const float close_width =
      UI::Editor::StudioEditorMetrics::editor_tab_close_width *
      layout.dpi_scale;
  for (std::size_t index = 0; index < m_tab_count; ++index) {
    const UI::Rect &tab_bounds = m_tab_bounds[index];
    const UI::Rect close_bounds{tab_bounds.right() - close_width, tab_bounds.y,
                                close_width, tab_bounds.height};
    if (tab_bounds.contains(point_x, point_y)) {
      hovered_tab = index;
      if (close_bounds.contains(point_x, point_y)) {
        hovered_close = index;
      }
      break;
    }
  }
  if (hovered_tab != m_hovered_tab_index ||
      hovered_close != m_hovered_tab_close_index) {
    m_hovered_tab_index = hovered_tab;
    m_hovered_tab_close_index = hovered_close;
    return true;
  }
  return scrollbar_changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (m_tab_drag_drop.is_dragging()) {
    const bool changed = m_tab_drag_drop.drag(point_x);
    float tab_x = layout.tab_bar_bounds.x;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (std::size_t index = 0; index < documents.size(); ++index) {
      const float width = UI::Editor::calculate_editor_tab_width(
          static_cast<float>(surface.m_ui_font->getTextWidth(
              std::string{documents[index].text.get_file_name()})),
          surface.m_dpi_scale);
      const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                            layout.tab_bar_bounds.height};
      if (bounds.contains(point_x, layout.tab_bar_bounds.y)) {
        if (m_tab_drag_drop.get_dragged_index() != index) {
          m_controller.reorder_file(m_tab_drag_drop.get_dragged_index(), index);
          m_tab_drag_drop.update_dragged_index(index);
        }
        break;
      }
      tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                           surface.m_dpi_scale;
    }
    return true;
  }

  UI::Editor::TextDocumentModel *document = m_controller.get_active_document();
  if (document != nullptr) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    const std::optional<std::size_t> target = m_minimap.handle_pointer_drag(
        layout, point_y, count_visible_lines(m_folding, total_lines),
        visible_count, m_scrollbar.get_first_visible_line());
    if (target) {
      static_cast<void>(m_scrollbar.scroll_to(*target));
      m_reveal_caret_pending = false;
      return true;
    }
  }
  if (m_scrollbar.handle_pointer_drag(layout, point_y)) {
    m_reveal_caret_pending = false;
    return true;
  }
  if (!m_pointer_selecting || document == nullptr) {
    return false;
  }
  const UI::Editor::TextPosition position =
      position_from_point(surface, layout, point_x, point_y);
  const bool changed =
      document->set_caret(position.line, position.column, true);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
  }
  return changed;
}

bool TextEditor::handle_pointer_release() noexcept {
  if (m_tab_drag_drop.is_dragging()) {
    m_tab_drag_drop.end_drag();
    return true;
  }

  const bool was_selecting = m_pointer_selecting;
  UI::Editor::TextDocumentModel *document = m_controller.get_active_document();
  if (document == nullptr) {
    bool changed = false;
    // We are passing 0,0 since we didn't track the actual release pos here, but
    // typically handle_pointer_release expects coordinates. Actually
    // handle_pointer_release currently doesn't take coordinates in
    // TextEditor.cpp, let's just pass dummy ones or reset state.
    m_empty_state_open_btn.set_pressed(false);
    m_empty_state_clone_btn.set_pressed(false);
  }
  m_pointer_selecting = false;
  const bool minimap_was_dragging = m_minimap.handle_pointer_release();
  const bool scrollbar_was_dragging = m_scrollbar.handle_pointer_release();
  return minimap_was_dragging || scrollbar_was_dragging || was_selecting;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::ptrdiff_t line_delta) noexcept {
  const UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document == nullptr) {
    if (layout.editor_bounds.contains(point_x, point_y)) {
      const float dpi = surface.m_dpi_scale;
      const int center_x = round_to_int(layout.editor_bounds.x +
                                        layout.editor_bounds.width * 0.5F);
      const int logo_size = round_to_int(150.0F * dpi);
      const int gap1 = round_to_int(30.0F * dpi);
      const int gap2 = round_to_int(40.0F * dpi);
      const int total_height =
          logo_size + gap1 + gap2 + 3 * round_to_int(28.0F * dpi);
      int current_y =
          round_to_int(layout.editor_bounds.y +
                       (layout.editor_bounds.height - total_height) * 0.5F);
      current_y += logo_size + gap1;
      current_y += gap2;

      const float btn_w = 300.0F * dpi;
      const float btn_h = 40.0F * dpi;
      const float btn_x = center_x - btn_w * 0.5F;

      m_empty_state_open_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);
      m_empty_state_clone_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});

      if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y)) {
        command_out = "zde.project.open";
        return true;
      }
      if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
        return true;
      }
    }
    return false;
  }
  const float line_height = 20.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  m_scrollbar.synchronize(document->get_line_count(), visible_count);
  m_reveal_caret_pending = false;
  return m_scrollbar.scroll_lines(line_delta);
}

bool TextEditor::handle_input(UI::Editor::EditorInputCommand command,
                              bool extend_selection) {
  const bool changed =
      m_focused && m_controller.execute_input(command, extend_selection);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
  }
  return changed;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action) {
  const bool changed = m_controller.execute_action(action);
  if (changed) {
    if (action == UI::Editor::EditorAction::CreateDocument ||
        action == UI::Editor::EditorAction::CloseDocument ||
        action == UI::Editor::EditorAction::RemoveDocument) {
      m_scrollbar.reset();
    }
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    if (action == UI::Editor::EditorAction::CreateDocument ||
        action == UI::Editor::EditorAction::CloseDocument ||
        action == UI::Editor::EditorAction::RemoveDocument) {
      m_hovered_tab_index.reset();
      m_hovered_tab_close_index.reset();
    }
  }
  return changed;
}

std::optional<bool> TextEditor::handle_command(std::string_view command_id) {
  const std::optional<UI::Editor::EditorAction> action =
      UI::Editor::EditorController::action_from_command_id(command_id);
  return action ? std::optional<bool>{handle_action(*action)} : std::nullopt;
}

std::optional<bool>
TextEditor::is_command_enabled(std::string_view command_id) const noexcept {
  const std::optional<UI::Editor::EditorAction> action =
      UI::Editor::EditorController::action_from_command_id(command_id);
  return action ? std::optional<bool>{m_controller.can_execute_action(*action)}
                : std::nullopt;
}

bool TextEditor::handle_text_input(std::string_view utf8_text) {
  const bool changed = m_focused && m_controller.insert_text(utf8_text);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
  }
  return changed;
}

bool TextEditor::is_focused() const noexcept { return m_focused; }

bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  return m_scrollbar.is_point(layout, point_x, point_y);
}

bool TextEditor::is_minimap_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  return m_minimap.is_point(layout, point_x, point_y);
}

bool TextEditor::tick_animations() noexcept {
  bool needs_redraw = m_focused &&
                      m_controller.get_active_document() != nullptr &&
                      m_caret_blink.tick();

  // Lerp animated tab positions
  bool animating = false;
  for (auto &[doc, animated_x] : m_tab_animated_x) {
    if (m_tab_target_x.contains(doc)) {
      const float target_x = m_tab_target_x[doc];
      if (std::abs(animated_x - target_x) > 0.5f) {
        animated_x += (target_x - animated_x) * 0.3f; // Smooth lerp
        animating = true;
      } else {
        animated_x = target_x;
      }
    }
  }

  if (m_selection_animation.tick()) {
    animating = true;
  }

  if (const UI::Editor::TextDocumentModel *doc =
          m_controller.get_active_document()) {
    UI::Editor::TextPosition current_caret{doc->get_caret_line(),
                                           doc->get_caret_column()};
    if (m_last_brace_caret != current_caret) {
      m_last_brace_caret = current_caret;
      auto [open_brace, close_brace] = find_enclosing_braces(*doc);
      m_brace_animation.set_active_braces(open_brace, close_brace);
    }
  } else {
    m_brace_animation.clear();
  }

  if (m_brace_animation.tick()) {
    animating = true;
  }

  return needs_redraw || animating;
}

const UI::Editor::TextDocumentModel *TextEditor::get_document() const noexcept {
  return m_controller.get_active_document();
}

void TextEditor::render(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  draw_tab_strip(surface, drawable, layout);
  draw_document(surface, drawable, layout);
  if (const UI::Editor::TextDocumentModel *document =
          m_controller.get_active_document()) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_minimap.render(surface, drawable, layout, *document,
                     m_scrollbar.get_first_visible_line(), visible_count);
  }
  m_scrollbar.render(surface, drawable, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  m_tab_count = 0;
  float tab_x = layout.tab_bar_bounds.x;
  const float right_limit = layout.tab_bar_bounds.right();
  const std::span<const UI::Editor::EditorSessionDocument> documents =
      m_controller.get_documents();
  const std::optional<std::size_t> active_index =
      m_controller.get_active_index();
  for (std::size_t index = 0; index < documents.size(); ++index) {
    const UI::Editor::TextDocumentModel &document = documents[index].text;
    const bool active = active_index && *active_index == index;
    const float width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{document.get_file_name()})),
        surface.m_dpi_scale);
    if (tab_x + width > right_limit) {
      break;
    }
    UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                    layout.tab_bar_bounds.height};
    m_tab_target_x[&document] = tab_x;

    if (m_tab_drag_drop.is_dragging() &&
        m_tab_drag_drop.get_dragged_index() == index) {
      bounds.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
    } else {
      if (!m_tab_animated_x.contains(&document)) {
        m_tab_animated_x[&document] = tab_x;
      }
      bounds.x = m_tab_animated_x[&document];
    }
    const std::size_t tab_index = m_tab_count;
    if (m_tab_count < max_visible_tabs) {
      m_tab_bounds[m_tab_count] = bounds;
      ++m_tab_count;
    }
    tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                         surface.m_dpi_scale;
  }

  auto draw_single_tab = [&](std::size_t tab_index) {
    const std::size_t index = tab_index; // Mapping is direct in the first pass
    const UI::Editor::TextDocumentModel &document = documents[index].text;
    const bool active = active_index && *active_index == index;
    const UI::Rect &bounds = m_tab_bounds[tab_index];
    const bool close_hovered =
        m_hovered_tab_close_index && *m_hovered_tab_close_index == tab_index;
    const bool tab_hovered =
        m_hovered_tab_index && *m_hovered_tab_index == tab_index;
    surface.fill_rectangle(
        drawable, bounds,
        active ? surface.m_pixels.tab_active_background
               : (tab_hovered ? surface.m_pixels.active_line_background
                              : surface.m_pixels.tab_background));
    const unsigned long tab_edge_color = surface.m_pixels.border;
    const int tab_left = round_to_int(bounds.x);
    const int tab_right = round_to_int(bounds.right()) - 1;
    const int tab_top = round_to_int(bounds.y);
    const int tab_bottom = round_to_int(bounds.bottom()) - 1;
    // Keep one flush top rule and vertical separators; there is no bottom
    // rule, so the titlebar remains visually open below the labels.
    surface.draw_line(drawable, tab_left, tab_top, tab_right, tab_top,
                      tab_edge_color);
    surface.draw_line(drawable, tab_left, tab_top, tab_left, tab_bottom,
                      tab_edge_color);
    surface.draw_line(drawable, tab_right, tab_top, tab_right, tab_bottom,
                      tab_edge_color);
    const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
        std::filesystem::path{std::string{document.get_file_name()}});
    surface.draw_svg_icon(
        drawable, icon_asset,
        round_to_int(
            bounds.x +
            (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
                surface.m_dpi_scale),
        round_to_int(bounds.y + bounds.height * 0.5F),
        std::max(round_to_int(14.0F * surface.m_dpi_scale), 10),
        surface.m_palette.text_primary, surface.m_palette.tab_background,
        false);
    surface.draw_text(
        drawable, *surface.m_ui_font, document.get_file_name(),
        bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                       surface.m_dpi_scale,
        bounds.y + bounds.height * 0.5F,
        active ? surface.m_text.primary : surface.m_text.muted);
    if (close_hovered) {
      surface.draw_svg_icon(
          drawable, "close-minimal.svg",
          round_to_int(bounds.right() -
                       UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                           0.5F * surface.m_dpi_scale),
          round_to_int(bounds.y + bounds.height * 0.5F),
          std::max(round_to_int(11.0F * surface.m_dpi_scale), 9),
          active ? surface.m_palette.text_primary
                 : surface.m_palette.text_muted,
          surface.m_palette.tab_background);
    } else if (document.is_dirty()) {
      surface.draw_svg_icon(
          drawable, "dirty.svg",
          round_to_int(bounds.right() -
                       UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                           0.5F * surface.m_dpi_scale),
          round_to_int(bounds.y + bounds.height * 0.5F),
          std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
          surface.m_palette.warning, surface.m_palette.tab_background);
    }
  };

  for (std::size_t tab_index = 0; tab_index < m_tab_count; ++tab_index) {
    if (m_tab_drag_drop.is_dragging() &&
        m_tab_drag_drop.get_dragged_index() == tab_index)
      continue;
    draw_single_tab(tab_index);
  }
  if (m_tab_drag_drop.is_dragging() &&
      m_tab_drag_drop.get_dragged_index() < m_tab_count) {
    draw_single_tab(m_tab_drag_drop.get_dragged_index());
  }

  const int tab_bar_bottom = round_to_int(layout.tab_bar_bounds.bottom()) - 1;
  const int tab_bar_left = round_to_int(layout.tab_bar_bounds.x);
  const int tab_bar_right = round_to_int(layout.tab_bar_bounds.right());

  if (active_index && *active_index < m_tab_count) {
    const UI::Rect &active_bounds = m_tab_bounds[*active_index];
    const int active_left = round_to_int(active_bounds.x);
    const int active_right = round_to_int(active_bounds.right()) - 1;

    surface.draw_line(drawable, tab_bar_left, tab_bar_bottom, active_left,
                      tab_bar_bottom, surface.m_pixels.border);
    surface.draw_line(drawable, active_right, tab_bar_bottom, tab_bar_right,
                      tab_bar_bottom, surface.m_pixels.border);
  } else {
    surface.draw_line(drawable, tab_bar_left, tab_bar_bottom, tab_bar_right,
                      tab_bar_bottom, surface.m_pixels.border);
  }
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document == nullptr) {
    const float dpi = surface.m_dpi_scale;
    const int center_x = round_to_int(layout.editor_bounds.x +
                                      layout.editor_bounds.width * 0.5F);

    const int logo_size = round_to_int(150.0F * dpi);
    const int gap1 = round_to_int(30.0F * dpi);
    const int gap2 = round_to_int(40.0F * dpi);
    const int line_height = round_to_int(28.0F * dpi);
    const int total_height = logo_size + gap1 + gap2 + 3 * line_height;

    int current_y =
        round_to_int(layout.editor_bounds.y +
                     (layout.editor_bounds.height - total_height) * 0.5F);

    surface.draw_png_icon(drawable, "zenvra_logo.png", center_x,
                          current_y + logo_size / 2, logo_size,
                          surface.m_palette.editor_background);
    current_y += logo_size + gap1;

    if (surface.m_ui_font) {
      const std::string title = "";
      if (surface.m_large_font) {
        int title_w = surface.m_large_font->getTextWidth(title);
        surface.draw_text(drawable, *surface.m_large_font, title,
                          static_cast<float>(center_x - title_w / 2),
                          static_cast<float>(current_y),
                          surface.m_text.primary);
      } else {
        int title_w = surface.m_ui_font->getTextWidth(title);
        surface.draw_text(drawable, *surface.m_ui_font, title,
                          static_cast<float>(center_x - title_w / 2),
                          static_cast<float>(current_y),
                          surface.m_text.primary);
      }
      current_y += gap2;

      const float btn_w = 300.0F * dpi;
      const float btn_h = 40.0F * dpi;
      const float btn_x = center_x - btn_w * 0.5F;

      // Layout using Flex for Open Folder
      m_empty_state_open_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      const auto &open_state = m_empty_state_open_btn.get_state();
      const unsigned long open_bg =
          open_state.pressed ? surface.m_pixels.accent
                             : (open_state.hovered ? surface.m_pixels.accent
                                                   : surface.m_pixels.accent);

      surface.fill_rounded_rectangle(
          drawable, m_empty_state_open_btn.get_bounds(), open_bg, 4.0F * dpi);

      const float icon_size = 16.0F * dpi;
      const float open_text_w =
          static_cast<float>(surface.m_ui_font->getTextWidth("Open Folder"));

      std::array<Utility::FlexItem, 2> flex_items_open{
          Utility::FlexItem::fixed(icon_size),
          Utility::FlexItem::fixed(open_text_w)};

      Utility::FlexOptions flex_opts;
      flex_opts.axis = Utility::LayoutAxis::Horizontal;
      flex_opts.justify_content = Utility::LayoutJustify::Center;
      flex_opts.align_items = Utility::LayoutAlign::Center;
      flex_opts.gap = 8.0F * dpi;

      Utility::FlexLayoutResult flex_res_open = Utility::Flex::calculate(
          m_empty_state_open_btn.get_bounds(), flex_items_open, flex_opts);

      surface.draw_svg_icon(drawable, "Assets/icons/folder.svg",
                            round_to_int(flex_res_open.items[0].x +
                                         flex_res_open.items[0].width * 0.5F),
                            round_to_int(flex_res_open.items[0].y +
                                         flex_res_open.items[0].height * 0.5F),
                            round_to_int(icon_size),
                            surface.m_palette.text_primary,
                            surface.m_palette.accent);

      surface.draw_text(
          drawable, *surface.m_ui_font, "Open Folder", flex_res_open.items[1].x,
          flex_res_open.items[1].y + flex_res_open.items[1].height * 0.5F,
          surface.m_text.primary);

      current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);

      // Clone Repository Button
      m_empty_state_clone_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      const auto &clone_state = m_empty_state_clone_btn.get_state();

      const unsigned long clone_bg =
          clone_state.pressed
              ? surface.m_pixels.accent
              : (clone_state.hovered ? surface.m_pixels.accent
                                     : surface.m_pixels.editor_background);
      surface.fill_rounded_rectangle(
          drawable, m_empty_state_clone_btn.get_bounds(), clone_bg, 4.0F * dpi);

      const float clone_text_w = static_cast<float>(
          surface.m_ui_font->getTextWidth("Clone Repository"));
      std::array<Utility::FlexItem, 2> flex_items_clone{
          Utility::FlexItem::fixed(icon_size),
          Utility::FlexItem::fixed(clone_text_w)};

      Utility::FlexLayoutResult flex_res_clone = Utility::Flex::calculate(
          m_empty_state_clone_btn.get_bounds(), flex_items_clone, flex_opts);

      surface.draw_svg_icon(
          drawable, "Assets/icons/copy.svg",
          round_to_int(flex_res_clone.items[0].x +
                       flex_res_clone.items[0].width * 0.5F),
          round_to_int(flex_res_clone.items[0].y +
                       flex_res_clone.items[0].height * 0.5F),
          round_to_int(icon_size), surface.m_palette.text_primary,
          clone_state.pressed ? surface.m_palette.tab_active_background
                              : surface.m_palette.tab_background);

      surface.draw_text(drawable, *surface.m_ui_font, "Clone Repository",
                        flex_res_clone.items[1].x,
                        flex_res_clone.items[1].y +
                            flex_res_clone.items[1].height * 0.5F,
                        surface.m_text.primary);
    }
    return;
  }
  const float line_height = 20.0F * surface.m_dpi_scale;
  const float first_center_y = layout.editor_bounds.y + line_height * 0.5F;
  const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines = document->get_line_count();

  // Rebuild folding model from the current document lines.
  m_folding.rebuild(std::vector<std::string>(document->get_lines().begin(),
                                             document->get_lines().end()));

  // The scrollbar tracks *visual* rows (hidden/folded lines are skipped).
  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);
  if (m_reveal_caret_pending) {
    static_cast<void>(m_scrollbar.reveal_line(physical_line_to_visual_row(
        m_folding, document->get_caret_line(), total_lines)));
    m_reveal_caret_pending = false;
  }
  // Map the scrollbar's visual first row back to the physical line it lands
  // on, then walk forward skipping hidden lines while drawing.
  const std::size_t first_line = visual_row_to_physical_line(
      m_folding, m_scrollbar.get_first_visible_line(), total_lines);
  const std::size_t first_visual_row = m_scrollbar.get_first_visible_line();
  const std::size_t render_count = visible_count;
  const bool syntax_highlighting =
      UI::Editor::supports_editor_syntax_highlighting(
          document->get_file_name());
  const auto token_color =
      [&surface](UI::Editor::EditorTokenKind kind) -> const std::string & {
    switch (kind) {
    case UI::Editor::EditorTokenKind::Keyword:
      return surface.m_text.keyword;
    case UI::Editor::EditorTokenKind::Number:
      return surface.m_text.number;
    case UI::Editor::EditorTokenKind::Label:
      return surface.m_text.label;
    case UI::Editor::EditorTokenKind::Type:
      return surface.m_text.type;
    case UI::Editor::EditorTokenKind::Comment:
      return surface.m_text.comment;
    case UI::Editor::EditorTokenKind::String:
      return surface.m_text.success;
    case UI::Editor::EditorTokenKind::Plain:
      return surface.m_text.primary;
    }
    return surface.m_text.primary;
  };

  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * surface.m_dpi_scale;
  const float gutter_line_x = layout.gutter_bounds.right() - fold_margin - 1.0F;
  surface.draw_line(
      drawable, round_to_int(gutter_line_x),
      round_to_int(layout.gutter_bounds.y), round_to_int(gutter_line_x),
      round_to_int(layout.gutter_bounds.bottom()), surface.m_pixels.border);

  // --- Indent guide rendering ---
  {
    // Width of a single space character in the editor font.
    const float space_width = static_cast<float>(
        surface.m_editor_font->getTextWidth(" "));
    const std::size_t last_line_in_view =
        visual_row_to_physical_line(m_folding, first_visual_row + render_count, total_lines);
    const auto guide_ranges =
        m_folding.get_indent_guide_ranges(first_line, last_line_in_view);
    const UI::Components::FoldRange* active_range =
        m_folding.get_active_indent_range(document->get_caret_line());

    for (const UI::Components::FoldRange* range : guide_ranges) {
      // The guide sits at the indent column of the opening brace line.
      // indent_level is already in "character columns" (tab=4 spaces).
      const float guide_x =
          code_x + static_cast<float>(range->indent_level) * space_width;

      // Skip guides that would land outside the editor area.
      if (guide_x < layout.editor_bounds.x ||
          guide_x > layout.editor_bounds.right()) {
        continue;
      }

      // Compute Y range from physical lines, converting to visual rows.
      // Use std::ptrdiff_t to prevent unsigned integer underflow when
      // range->start_line is above the current viewport top (first_visual_row).
      const std::ptrdiff_t start_vis_row = static_cast<std::ptrdiff_t>(
          physical_line_to_visual_row(m_folding, range->start_line + 1, total_lines));
      const std::ptrdiff_t end_vis_row = static_cast<std::ptrdiff_t>(
          physical_line_to_visual_row(m_folding, range->end_line, total_lines));
      const std::ptrdiff_t first_vis_row = static_cast<std::ptrdiff_t>(first_visual_row);

      const float y_top_raw =
          layout.editor_bounds.y +
          static_cast<float>(start_vis_row - first_vis_row) * line_height;
      const float y_bottom_raw =
          layout.editor_bounds.y +
          static_cast<float>(end_vis_row - first_vis_row + 1) * line_height;

      // Clip to the editor viewport.
      const float y_top =
          std::max(y_top_raw, layout.editor_bounds.y);
      const float y_bottom =
          std::min(y_bottom_raw, layout.editor_bounds.bottom());
      if (y_top >= y_bottom) {
        continue;
      }

      const bool is_active =
          active_range != nullptr &&
          range->start_line == active_range->start_line &&
          range->end_line == active_range->end_line;

      surface.draw_line(
          drawable, round_to_int(guide_x), round_to_int(y_top),
          round_to_int(guide_x), round_to_int(y_bottom),
          is_active ? surface.m_pixels.indent_guide_active
                    : surface.m_pixels.indent_guide);
    }
  }

  // Collect selection target rects (relative to code_x) and feed the animation
  // model.
  std::vector<UI::Rect> selection_targets;
  if (document->has_selection()) {
    const UI::Editor::TextSelection selection = document->get_selection();
    const std::size_t start_line = selection.start.line;
    const std::size_t end_line =
        std::min(selection.end.line, start_line + 1000);

    for (std::size_t line_index = start_line; line_index <= end_line;
         ++line_index) {
      const std::string_view line = document->get_line(line_index);
      const std::size_t selection_start =
          line_index == selection.start.line ? selection.start.column : 0;
      const std::size_t selection_end =
          line_index == selection.end.line ? selection.end.column : line.size();

      const float selection_x =
          static_cast<float>(surface.m_editor_font->getTextWidth(
              std::string{line.substr(0, selection_start)}));
      float selection_width =
          static_cast<float>(surface.m_editor_font->getTextWidth(std::string{
              line.substr(selection_start, selection_end - selection_start)}));

      if (line_index < selection.end.line) {
        selection_width += 6.0F * surface.m_dpi_scale;
      }
      selection_width = std::min(
          selection_width,
          std::max(layout.editor_bounds.right() - code_x - selection_x, 0.0F));

      selection_targets.push_back(
          UI::Rect{selection_x,
                   static_cast<float>(physical_line_to_visual_row(
                       m_folding, line_index, total_lines)) *
                       line_height,
                   selection_width, line_height});
    }
  } else {
    m_selection_animation.clear();
  }
  if (!selection_targets.empty()) {
    m_selection_animation.set_targets(selection_targets);
  }

  if (m_selection_animation.has_rects()) {
    const float radius = 3.0F * surface.m_dpi_scale;

    for (const UI::Rect &anim_rect :
         m_selection_animation.get_animated_rects()) {
      if (anim_rect.width <= 0.0F)
        continue;

      const float screen_y = layout.editor_bounds.y + anim_rect.y -
                             static_cast<float>(first_visual_row) * line_height;
      const float screen_x = code_x + anim_rect.x;

      if (screen_y + anim_rect.height >= layout.editor_bounds.y &&
          screen_y <= layout.editor_bounds.bottom()) {
        surface.fill_rounded_rectangle(
            drawable,
            UI::Rect{screen_x, screen_y, anim_rect.width, anim_rect.height},
            surface.m_pixels.selection_background, radius);
      }
    }
  }

  std::size_t row = 0;
  for (std::size_t line_index = first_line;
       row < render_count && line_index < total_lines; ++line_index) {
    if (m_folding.is_line_hidden(line_index)) {
      continue;
    }
    const std::string_view line = document->get_line(line_index);
    const float center_y =
        first_center_y + static_cast<float>(row) * line_height;
    ++row;
    const bool active_line = line_index == document->get_caret_line();
    if (active_line) {
      surface.fill_rectangle(
          drawable,
          UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                   layout.editor_bounds.right() - layout.gutter_bounds.x,
                   line_height},
          surface.m_pixels.active_line_background);
    }
    const std::string number = std::to_string(line_index + 1);
    // Tuck the line number in tight against the fold margin so the gutter
    // reads as one continuous column rather than three disjoint blocks.
    const float number_x =
        layout.gutter_bounds.right() - fold_margin -
        4.0F * surface.m_dpi_scale -
        static_cast<float>(surface.m_small_font->getTextWidth(number));
    surface.draw_text(
        drawable, *surface.m_small_font, number, number_x, center_y,
        active_line ? surface.m_text.primary : surface.m_text.muted);
    if (has_gutter_marker(line)) {
      const int marker_x = round_to_int(layout.gutter_bounds.right() -
                                        13.0F * surface.m_dpi_scale);
      const int marker_y = round_to_int(center_y);
      const int half = std::max(round_to_int(3.0F * surface.m_dpi_scale), 2);
      XPoint points[]{
          XPoint{static_cast<short>(marker_x),
                 static_cast<short>(marker_y - half)},
          XPoint{static_cast<short>(marker_x + half),
                 static_cast<short>(marker_y)},
          XPoint{static_cast<short>(marker_x),
                 static_cast<short>(marker_y + half)},
          XPoint{static_cast<short>(marker_x - half),
                 static_cast<short>(marker_y)},
          XPoint{static_cast<short>(marker_x),
                 static_cast<short>(marker_y - half)},
      };
      XSetForeground(surface.m_display, surface.m_graphics_context,
                     surface.m_pixels.text_muted);
      XDrawLines(surface.m_display, drawable, surface.m_graphics_context,
                 points, 5, CoordModeOrigin);
    }

    // --- Fold icon and scope guide rendering ---
    const UI::Components::FoldMarker fold_marker =
        m_folding.get_marker(line_index);
    const float fold_center_x =
        layout.gutter_bounds.right() - fold_margin * 0.5F;
    const int fold_cx = round_to_int(fold_center_x);
    const int fold_cy = round_to_int(center_y);

    if (fold_marker == UI::Components::FoldMarker::Expanded ||
        fold_marker == UI::Components::FoldMarker::Collapsed) {
      const int box_half =
          std::max(round_to_int(5.0F * surface.m_dpi_scale), 4);
      const bool fold_hovered =
          m_hovered_fold_line && *m_hovered_fold_line == line_index;

      // Box background. Match the active-line highlight when present so the
      // fold widget blends into the gutter instead of sitting on a flat box.
      surface.fill_rectangle(
          drawable,
          UI::Rect{static_cast<float>(fold_cx - box_half),
                   static_cast<float>(fold_cy - box_half),
                   static_cast<float>(box_half * 2),
                   static_cast<float>(box_half * 2)},
          active_line ? surface.m_pixels.active_line_background
                      : surface.m_pixels.editor_background);

      // Box border.
      surface.draw_rectangle(
          drawable,
          UI::Rect{static_cast<float>(fold_cx - box_half),
                   static_cast<float>(fold_cy - box_half),
                   static_cast<float>(box_half * 2),
                   static_cast<float>(box_half * 2)},
          fold_hovered ? surface.m_pixels.accent : surface.m_pixels.border);

      // Horizontal line of +/- (always present).
      const int sign_inset =
          std::max(round_to_int(2.0F * surface.m_dpi_scale), 2);
      surface.draw_line(drawable, fold_cx - box_half + sign_inset, fold_cy,
                        fold_cx + box_half - sign_inset, fold_cy,
                        fold_hovered ? surface.m_pixels.accent
                                     : surface.m_pixels.text_muted);

      if (fold_marker == UI::Components::FoldMarker::Collapsed) {
        // Vertical line of + (only when collapsed).
        surface.draw_line(drawable, fold_cx, fold_cy - box_half + sign_inset,
                          fold_cx, fold_cy + box_half - sign_inset,
                          fold_hovered ? surface.m_pixels.accent
                                       : surface.m_pixels.text_muted);
      }
    } else if (fold_marker == UI::Components::FoldMarker::Continuation) {
      surface.draw_line(drawable, fold_cx,
                        round_to_int(center_y - line_height * 0.5F), fold_cx,
                        round_to_int(center_y + line_height * 0.5F),
                        surface.m_pixels.border);
    } else if (fold_marker == UI::Components::FoldMarker::End) {
      surface.draw_line(drawable, fold_cx,
                        round_to_int(center_y - line_height * 0.5F), fold_cx,
                        fold_cy, surface.m_pixels.border);
      surface.draw_line(drawable, fold_cx, fold_cy,
                        fold_cx + round_to_int(fold_margin * 0.35F), fold_cy,
                        surface.m_pixels.border);
    }

    // Connect expanded marker to continuation line below.
    if (fold_marker == UI::Components::FoldMarker::Expanded) {
      const int box_half =
          std::max(round_to_int(5.0F * surface.m_dpi_scale), 4);
      surface.draw_line(drawable, fold_cx, fold_cy + box_half, fold_cx,
                        round_to_int(center_y + line_height * 0.5F),
                        surface.m_pixels.border);
    }
    if (syntax_highlighting) {
      float token_x = code_x;
      std::size_t rendered_bytes = 0;
      std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
          tokens{};
      const std::size_t token_count =
          UI::Editor::tokenize_editor_line(line, tokens);
      for (std::size_t token_index = 0; token_index < token_count;
           ++token_index) {
        const UI::Editor::EditorToken &token = tokens[token_index];

        bool has_animated_brace = false;
        std::size_t brace_offset = 0;

        if (m_brace_animation.has_active_braces() &&
            m_brace_animation.get_pulse_scale() > 1.01F) {
          if (auto open_pos = m_brace_animation.get_open_brace();
              open_pos && open_pos->line == line_index) {
            if (open_pos->column >= rendered_bytes &&
                open_pos->column < rendered_bytes + token.text.size()) {
              has_animated_brace = true;
              brace_offset = open_pos->column - rendered_bytes;
            }
          }
          if (auto close_pos = m_brace_animation.get_close_brace();
              close_pos && close_pos->line == line_index) {
            if (close_pos->column >= rendered_bytes &&
                close_pos->column < rendered_bytes + token.text.size()) {
              has_animated_brace = true;
              brace_offset = close_pos->column - rendered_bytes;
            }
          }
        }

        if (has_animated_brace) {
          if (brace_offset > 0) {
            const std::string pre{token.text.substr(0, brace_offset)};
            surface.draw_text(drawable, *surface.m_editor_font, pre, token_x,
                              center_y, token_color(token.kind));
            token_x +=
                static_cast<float>(surface.m_editor_font->getTextWidth(pre));
          }

          const std::string brace_char{token.text.substr(brace_offset, 1)};
          const float pulse = m_brace_animation.get_pulse_scale();
          const float brace_w = static_cast<float>(
              surface.m_editor_font->getTextWidth(brace_char));

          const float extra_w = (brace_w * pulse - brace_w) * 0.5F;
          const float extra_h = (line_height * pulse - line_height) * 0.5F;
          const float screen_y = center_y - line_height * 0.5F;

          if (!m_brace_pulse_color_ready) {
            UI::Theme::Color pulse_color =
                surface.m_palette.selection_background;
            pulse_color.red =
                static_cast<std::uint8_t>(std::min(pulse_color.red + 30, 255));
            pulse_color.green = static_cast<std::uint8_t>(
                std::min(pulse_color.green + 30, 255));
            pulse_color.blue =
                static_cast<std::uint8_t>(std::min(pulse_color.blue + 30, 255));
            m_brace_pulse_color = surface.allocate_color(pulse_color);
            m_brace_pulse_color_ready = true;
          }

          surface.fill_rounded_rectangle(
              drawable,
              UI::Rect{token_x - extra_w - 2.0F, screen_y - extra_h,
                       brace_w + extra_w * 2.0F + 4.0F,
                       line_height + extra_h * 2.0F},
              m_brace_pulse_color, 3.0F * surface.m_dpi_scale * pulse);

          // X11 cannot scale glyphs; draw the brace at normal size over the
          // pulse box.
          surface.draw_text(drawable, *surface.m_editor_font, brace_char,
                            token_x, center_y, surface.m_text.accent);
          token_x += brace_w;

          if (brace_offset + 1 < token.text.size()) {
            const std::string post{token.text.substr(brace_offset + 1)};
            surface.draw_text(drawable, *surface.m_editor_font, post, token_x,
                              center_y, token_color(token.kind));
            token_x +=
                static_cast<float>(surface.m_editor_font->getTextWidth(post));
          }
        } else {
          const std::string text{token.text};
          surface.draw_text(drawable, *surface.m_editor_font, text, token_x,
                            center_y, token_color(token.kind));
          token_x +=
              static_cast<float>(surface.m_editor_font->getTextWidth(text));
        }
        rendered_bytes += token.text.size();
      }
      if (rendered_bytes < line.size()) {
        surface.draw_text(drawable, *surface.m_editor_font,
                          line.substr(rendered_bytes), token_x, center_y,
                          surface.m_text.primary);
      }
    } else {
      surface.draw_text(drawable, *surface.m_editor_font, line, code_x,
                        center_y, surface.m_text.primary);
    }
    if (active_line && m_focused && m_caret_blink.is_visible()) {
      const std::string_view prefix =
          line.substr(0, document->get_caret_column());
      const int caret_x = round_to_int(
          code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                       std::string{prefix})));
      surface.draw_line(drawable, caret_x,
                        round_to_int(center_y - 8.0F * surface.m_dpi_scale),
                        caret_x,
                        round_to_int(center_y + 8.0F * surface.m_dpi_scale),
                        surface.m_pixels.text_primary);
    }
  }
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const {
  const UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document == nullptr) {
    return {};
  }
  const float line_height = 20.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines = document->get_line_count();
  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);
  const std::size_t first_line = m_scrollbar.get_first_visible_line();
  const float clamped_y = std::clamp(
      point_y, layout.editor_bounds.y,
      std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
  const std::size_t clicked_row = static_cast<std::size_t>(std::max(
      static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
  const std::size_t line_index = visual_row_to_physical_line(
      m_folding, first_line + clicked_row, total_lines);
  const std::string_view line = document->get_line(line_index);
  const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale;
  const float target_x = std::max(point_x - code_x, 0.0F);
  std::size_t column = 0;
  int previous_width = 0;
  while (column < line.size()) {
    const std::size_t next_column = next_character_column(line, column);
    const int next_width = surface.m_editor_font->getTextWidth(
        std::string{line.substr(0, next_column)});
    if (target_x < static_cast<float>(previous_width + next_width) * 0.5F) {
      break;
    }
    column = next_column;
    previous_width = next_width;
  }
  return {line_index, column};
}

} // namespace Zenvra::Platform::X11::Components
