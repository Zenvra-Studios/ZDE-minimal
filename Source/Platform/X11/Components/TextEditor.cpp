#include "Platform/X11/Components/TextEditor.h"
#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

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

std::size_t
visual_row_to_physical_line(const UI::Components::EditorFoldingModel &folding,
                            std::size_t visual_row, std::size_t total_lines) {
  std::size_t current_visual = 0;
  for (std::size_t i = 0; i < total_lines; ++i) {
    if (!folding.is_line_hidden(i)) {
      if (current_visual == visual_row)
        return i;
      current_visual++;
    }
  }
  return total_lines > 0 ? total_lines - 1 : 0;
}

std::size_t
physical_line_to_visual_row(const UI::Components::EditorFoldingModel &folding,
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

std::size_t
count_visible_lines(const UI::Components::EditorFoldingModel &folding,
                    std::size_t total_lines) {
  std::size_t visible = 0;
  for (std::size_t i = 0; i < total_lines; ++i) {
    if (!folding.is_line_hidden(i))
      visible++;
  }
  return visible;
}

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

  int brace_depth = 0;
  bool found_open = false;
  const int search_limit = std::max(0, static_cast<int>(start_line) - 500);

  for (int line_idx = static_cast<int>(start_line); line_idx >= search_limit;
       --line_idx) {
    std::string_view line =
        document.get_line(static_cast<std::size_t>(line_idx));

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

std::string make_lsp_uri(std::string_view filename) {
  if (filename.empty() || filename.starts_with("Untitled") ||
      filename.starts_with("untitled")) {
    return "file:///untitled.cpp";
  }
  std::error_code ec;
  std::filesystem::path p(filename);
  if (!p.is_absolute()) {
    p = std::filesystem::current_path() / p;
  }
  p = std::filesystem::weakly_canonical(p, ec);
  std::string generic = p.generic_string();
  if (!generic.starts_with("/")) {
    generic = "/" + generic;
  }
  return "file://" + generic;
}

} // namespace

std::string TextEditor::get_active_document_uri() const {
  if (const auto *path = m_controller.get_active_path()) {
    return make_lsp_uri(path->string());
  }
  if (const auto *doc = m_controller.get_active_document()) {
    return make_lsp_uri(doc->get_file_name());
  }
  return "file:///untitled.cpp";
}

std::string TextEditor::get_active_document_filename() const {
  if (const auto *path = m_controller.get_active_path()) {
    return path->string();
  }
  if (const auto *doc = m_controller.get_active_document()) {
    return std::string{doc->get_file_name()};
  }
  return "untitled.cpp";
}

void TextEditor::on_diagnostics_updated(
    const std::string &uri,
    std::vector<Language::Protocol::Diagnostic> diags) {
  std::lock_guard<std::mutex> lock(m_lsp_mutex);
  if (auto *doc = m_controller.get_active_document()) {
    const std::string current_uri = get_active_document_uri();
    if (current_uri == uri || uri.ends_with(get_active_document_filename())) {
      doc->set_diagnostics(std::move(diags));
    }
  }
}

bool TextEditor::open_file(const std::filesystem::path &path) {
  const bool opened = m_controller.open_file(path);
  if (opened) {
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();

    if (const auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_opened(
          uri, fname, 1, content);

      auto diags =
          Language::LanguageServerManager::instance().get_diagnostics_for_document(
              uri);
      if (!diags.empty()) {
        const_cast<UI::Editor::TextDocumentModel *>(doc)->set_diagnostics(
            std::move(diags));
      }
    }
  }
  return opened;
}

bool TextEditor::close_file(const std::filesystem::path &path) {
  const auto docs = m_controller.get_documents();
  for (std::size_t i = 0; i < docs.size(); ++i) {
    if (docs[i].path == path) {
      const std::string uri = make_lsp_uri(path.string());
      Language::LanguageServerManager::instance().on_document_closed(
          uri, path.filename().string());
      return m_controller.close_file(i);
    }
  }
  return false;
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

    if (const auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_opened(
          uri, fname, 1, content);

      auto diags =
          Language::LanguageServerManager::instance().get_diagnostics_for_document(
              uri);
      if (!diags.empty()) {
        const_cast<UI::Editor::TextDocumentModel *>(doc)->set_diagnostics(
            std::move(diags));
      }
    }
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

    if (const auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_opened(
          uri, fname, 1, content);

      auto diags =
          Language::LanguageServerManager::instance().get_diagnostics_for_document(
              uri);
      if (!diags.empty()) {
        const_cast<UI::Editor::TextDocumentModel *>(doc)->set_diagnostics(
            std::move(diags));
      }
    }
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

  float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
  const float right_limit = layout.tab_bar_bounds.right();
  const std::span<const UI::Editor::EditorSessionDocument> documents =
      m_controller.get_documents();
  for (const UI::Editor::EditorSessionDocument &document : documents) {
    const float width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{document.text.get_file_name()})),
        surface.m_dpi_scale);
    if (tab_x > right_limit) {
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
    if (m_max_tab_scroll > 0.0F) {
      const float track_width = layout.tab_bar_bounds.width;
      const float thumb_width = std::max(
          20.0F * layout.dpi_scale,
          track_width * (track_width / (track_width + m_max_tab_scroll)));
      const float thumb_x =
          layout.tab_bar_bounds.x +
          (m_tab_scroll_offset / m_max_tab_scroll) * (track_width - thumb_width);
      const UI::Rect thumb_bounds{
          thumb_x, layout.tab_bar_bounds.bottom() - 3.0F * layout.dpi_scale,
          thumb_width, 3.0F * layout.dpi_scale};
      const UI::Rect hit_bounds{
          thumb_bounds.x, thumb_bounds.y - 2.0F * layout.dpi_scale,
          thumb_bounds.width, thumb_bounds.height + 2.0F * layout.dpi_scale};
      if (hit_bounds.contains(point_x, point_y)) {
        return true;
      }
    }

    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const float right_limit = layout.tab_bar_bounds.right();
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (std::size_t index = 0; index < documents.size(); ++index) {
      const float width = UI::Editor::calculate_editor_tab_width(
          static_cast<float>(surface.m_ui_font->getTextWidth(
              std::string{documents[index].text.get_file_name()})),
          surface.m_dpi_scale);
      if (tab_x > right_limit) {
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
          if (m_controller.close_file(index)) {
            m_hovered_tab_index.reset();
            m_hovered_tab_close_index.reset();
            m_scrollbar.reset();
            m_reveal_caret_pending = true;
            return true;
          }
          return false;
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
  }

  UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document != nullptr && m_minimap.is_point(layout, point_x, point_y)) {
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
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
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
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
      const float full_height =
          layout.editor_bounds.height + layout.terminal_panel_bounds.height;
      int current_y = round_to_int(layout.editor_bounds.y +
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
        command_out = "zde.git.clone";
        return true;
      }
    }
  }

  if ((!layout.gutter_bounds.contains(point_x, point_y) &&
       !layout.editor_bounds.contains(point_x, point_y)) ||
      document == nullptr) {
    return false;
  }

  const float line_height = 20.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(std::max(
      static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines = document->get_line_count();
  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);

  if (const std::optional<std::size_t> fold_line = fold_start_line_at_point(
          m_folding, layout, point_x, point_y, surface.m_dpi_scale,
          m_scrollbar.get_first_visible_line(), total_lines)) {
    m_folding.toggle_fold(*fold_line);
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
    m_reveal_caret_pending = true;
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(m_lsp_mutex);
    m_completion_popup.hide();
    m_signature_help.hide();
    m_hover_tooltip.hide();
  }

  m_focused = true;
  m_pointer_selecting = true;
  const UI::Editor::TextPosition position =
      position_from_point(surface, layout, point_x, point_y);

  if (click_count == 2) {
    const std::string_view line = document->get_line(position.line);
    std::size_t start = position.column;
    std::size_t end = position.column;
    while (start > 0 &&
           (std::isalnum(static_cast<unsigned char>(line[start - 1])) ||
            line[start - 1] == '_')) {
      --start;
    }
    while (end < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[end])) ||
            line[end] == '_')) {
      ++end;
    }
    static_cast<void>(document->set_caret(position.line, start, false));
    static_cast<void>(document->set_caret(position.line, end, true));
  } else if (click_count >= 3) {
    const std::string_view line = document->get_line(position.line);
    static_cast<void>(document->set_caret(position.line, 0, false));
    static_cast<void>(document->set_caret(position.line, line.size(), true));
  } else {
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

  bool changed = false;
  if (hovered_tab != m_hovered_tab_index ||
      hovered_close != m_hovered_tab_close_index) {
    m_hovered_tab_index = hovered_tab;
    m_hovered_tab_close_index = hovered_close;
    changed = true;
  }
  return scrollbar_changed || changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (m_tab_drag_drop.is_dragging()) {
    static_cast<void>(m_tab_drag_drop.drag(point_x));
    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (std::size_t index = 0; index < documents.size(); ++index) {
      const float width = UI::Editor::calculate_editor_tab_width(
          static_cast<float>(surface.m_ui_font->getTextWidth(
              std::string{documents[index].text.get_file_name()})),
          surface.m_dpi_scale);
      const UI::Rect bounds{
          tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
      if (bounds.contains(point_x, layout.tab_bar_bounds.y)) {
        if (m_tab_drag_drop.get_dragged_index() != index) {
          static_cast<void>(m_controller.reorder_file(
              m_tab_drag_drop.get_dragged_index(), index));
          m_tab_drag_drop.update_dragged_index(index);
        }
        break;
      }
      tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                           surface.m_dpi_scale;
    }
    return true;
  }

  if (m_scrollbar.is_point(layout, point_x, point_y)) {
    return m_scrollbar.handle_pointer_drag(layout, point_y);
  }

  if (m_pointer_selecting) {
    UI::Editor::TextDocumentModel *document =
        m_controller.get_active_document();
    if (document != nullptr) {
      const UI::Editor::TextPosition position =
          position_from_point(surface, layout, point_x, point_y);
      static_cast<void>(
          document->set_caret(position.line, position.column, true));
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
      return true;
    }
  }
  return false;
}

bool TextEditor::handle_pointer_release() noexcept {
  if (m_tab_drag_drop.is_dragging()) {
    m_tab_drag_drop.end_drag();
    return true;
  }
  m_pointer_selecting = false;
  return m_scrollbar.handle_pointer_release();
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, std::string &command_out, std::ptrdiff_t line_delta,
    bool horizontal) noexcept {
  static_cast<void>(command_out);

  {
    std::lock_guard<std::mutex> lock(m_lsp_mutex);
    if (m_completion_popup.is_visible() &&
        m_completion_popup.is_point_inside(point_x, point_y,
                                           24.0F * surface.m_dpi_scale,
                                           340.0F * surface.m_dpi_scale)) {
      return m_completion_popup.scroll(static_cast<int>(line_delta));
    }
  }

  if (horizontal && line_delta != 0) {
    const float speed = 32.0F * layout.dpi_scale;
    if (layout.editor_bounds.contains(point_x, point_y)) {
      m_text_scroll_offset += static_cast<float>(line_delta) * speed;
      m_text_scroll_offset =
          std::clamp(m_text_scroll_offset, 0.0F, m_max_text_scroll);
      return true;
    }
    return false;
  }

  if (!layout.editor_bounds.contains(point_x, point_y) &&
      !layout.gutter_bounds.contains(point_x, point_y) &&
      !layout.minimap_bounds.contains(point_x, point_y) &&
      !layout.scrollbar_bounds.contains(point_x, point_y) &&
      !m_scrollbar.is_point(layout, point_x, point_y)) {
    return false;
  }

  const UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document == nullptr) {
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
  {
    std::lock_guard<std::mutex> lock(m_lsp_mutex);
    if (m_completion_popup.is_visible()) {
      if (command == UI::Editor::EditorInputCommand::MoveUp) {
        m_completion_popup.select_previous();
        return true;
      }
      if (command == UI::Editor::EditorInputCommand::MoveDown) {
        m_completion_popup.select_next();
        return true;
      }
      if (command == UI::Editor::EditorInputCommand::Escape ||
          command == UI::Editor::EditorInputCommand::MoveLeft ||
          command == UI::Editor::EditorInputCommand::MoveRight ||
          command == UI::Editor::EditorInputCommand::MoveHome ||
          command == UI::Editor::EditorInputCommand::MoveEnd) {
        m_completion_popup.hide();
        m_signature_help.hide();
        if (command == UI::Editor::EditorInputCommand::Escape) {
          return true;
        }
      }
      if (command == UI::Editor::EditorInputCommand::InsertTab ||
          command == UI::Editor::EditorInputCommand::InsertNewLine) {
        if (const auto *item = m_completion_popup.get_selected_item()) {
          if (auto *doc = m_controller.get_active_document(); doc != nullptr) {
            const std::string_view current_line =
                doc->get_line(doc->get_caret_line());
            const std::size_t caret_col = doc->get_caret_column();

            std::size_t prefix_len = 0;
            const std::string_view line_to_caret =
                current_line.substr(0, std::min(caret_col, current_line.size()));
            const std::size_t inc_idx = line_to_caret.find("#include");
            const std::size_t imp_idx = line_to_caret.find("#import");
            if (inc_idx != std::string_view::npos ||
                imp_idx != std::string_view::npos) {
              const std::size_t last_lt = line_to_caret.rfind('<');
              const std::size_t last_q = line_to_caret.rfind('"');
              if (last_lt != std::string_view::npos &&
                  (line_to_caret.rfind('>') == std::string_view::npos ||
                   last_lt > line_to_caret.rfind('>'))) {
                prefix_len = line_to_caret.size() - (last_lt + 1);
              } else if (last_q != std::string_view::npos &&
                         (std::count(line_to_caret.begin(),
                                     line_to_caret.end(), '"') % 2 == 1)) {
                prefix_len = line_to_caret.size() - (last_q + 1);
              }
            }
            if (prefix_len == 0) {
              std::size_t word_start = std::min(caret_col, current_line.size());
              while (word_start > 0) {
                const char c = current_line[word_start - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                    c == '#' || c == '~') {
                  --word_start;
                } else {
                  break;
                }
              }
              prefix_len = caret_col - word_start;
            }

            for (std::size_t k = 0; k < prefix_len; ++k) {
              static_cast<void>(m_controller.execute_input(
                  UI::Editor::EditorInputCommand::DeleteBackward));
            }

            std::string text_to_insert = item->insert_text;
            const std::size_t start_line = doc->get_caret_line();
            const std::size_t start_col = doc->get_caret_column();

            std::size_t target_cursor_line = start_line;
            std::size_t target_cursor_col = start_col;
            bool cursor_targeted = false;

            std::size_t first_placeholder_pos = std::string::npos;
            while (true) {
              const std::size_t p_start = text_to_insert.find("${");
              if (p_start == std::string::npos)
                break;
              const std::size_t p_end = text_to_insert.find('}', p_start);
              if (p_end == std::string::npos)
                break;

              const std::string inner = text_to_insert.substr(
                  p_start + 2, p_end - (p_start + 2));
              std::string def_val;
              const std::string tag = "${" + inner + "}";
              const std::size_t colon = inner.find(':');
              if (colon != std::string::npos) {
                def_val = inner.substr(colon + 1);
              }

              if (first_placeholder_pos == std::string::npos) {
                first_placeholder_pos = p_start;
              }

              std::size_t search_pos = 0;
              while ((search_pos = text_to_insert.find(tag, search_pos)) !=
                     std::string::npos) {
                text_to_insert.replace(search_pos, tag.size(), def_val);
                search_pos += def_val.size();
              }
            }

            const std::size_t tabstop_pos = text_to_insert.find("$0");
            if (tabstop_pos != std::string::npos) {
              std::size_t line_offset = 0;
              std::size_t last_nl = 0;
              for (std::size_t idx = 0; idx < tabstop_pos; ++idx) {
                if (text_to_insert[idx] == '\n') {
                  ++line_offset;
                  last_nl = idx + 1;
                }
              }
              const std::size_t col_offset = tabstop_pos - last_nl;
              target_cursor_line = start_line + line_offset;
              target_cursor_col =
                  (line_offset == 0) ? start_col + col_offset : col_offset;
              cursor_targeted = true;

              text_to_insert.erase(tabstop_pos, 2);
            } else if (first_placeholder_pos != std::string::npos) {
              std::size_t line_offset = 0;
              std::size_t last_nl = 0;
              for (std::size_t idx = 0; idx < first_placeholder_pos; ++idx) {
                if (text_to_insert[idx] == '\n') {
                  ++line_offset;
                  last_nl = idx + 1;
                }
              }
              const std::size_t col_offset = first_placeholder_pos - last_nl;
              target_cursor_line = start_line + line_offset;
              target_cursor_col =
                  (line_offset == 0) ? start_col + col_offset : col_offset;
              cursor_targeted = true;
            }

            static_cast<void>(m_controller.insert_text(text_to_insert));

            if (cursor_targeted) {
              const std::size_t max_lines = doc->get_line_count();
              if (target_cursor_line < max_lines) {
                const std::size_t line_len =
                    doc->get_line(target_cursor_line).size();
                doc->set_caret(target_cursor_line,
                               std::min(target_cursor_col, line_len));
              }
            }

            m_completion_popup.hide();
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            return true;
          }
        }
      }
    }
  }

  const bool changed =
      m_focused && m_controller.execute_input(command, extend_selection);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();

    if (auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_changed(
          uri, fname, 1, content);

      if (command == UI::Editor::EditorInputCommand::InsertNewLine ||
          command == UI::Editor::EditorInputCommand::MoveUp ||
          command == UI::Editor::EditorInputCommand::MoveDown) {
        std::lock_guard<std::mutex> lock(m_lsp_mutex);
        m_signature_help.hide();
      }
    }
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
  if (command_id == "editor.save") {
    if (const auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      Language::LanguageServerManager::instance().on_document_saved(uri, fname);
    }
  }

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
  if (utf8_text.empty() || !m_focused) {
    return false;
  }

  auto *doc = m_controller.get_active_document();
  if (doc == nullptr) {
    return false;
  }

  // Auto-close matching braces/quotes
  if (utf8_text == "(" || utf8_text == "[" || utf8_text == "{" ||
      utf8_text == "\"" || utf8_text == "'") {
    std::string pair_text{utf8_text};
    if (utf8_text == "(") pair_text += ")";
    else if (utf8_text == "[") pair_text += "]";
    else if (utf8_text == "{") pair_text += "}";
    else if (utf8_text == "\"") pair_text += "\"";
    else if (utf8_text == "'") pair_text += "'";

    if (m_controller.insert_text(pair_text)) {
      static_cast<void>(m_controller.execute_input(
          UI::Editor::EditorInputCommand::MoveLeft));
      m_reveal_caret_pending = true;
      m_caret_blink.reset();

      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_changed(
          uri, fname, 1, content);

      if (utf8_text == "(") {
        Language::Protocol::Position sig_pos{
            .line = doc->get_caret_line(),
            .character = doc->get_caret_column()};
        Language::LanguageServerManager::instance().request_signature_help(
            uri, fname, sig_pos, doc->get_line(doc->get_caret_line()),
            [this](std::optional<Language::Protocol::SignatureHelp> help) {
              std::lock_guard<std::mutex> lock(m_lsp_mutex);
              if (help.has_value() && !help->signatures.empty()) {
                m_signature_help.show(std::move(*help), 0.0F, 0.0F);
              } else {
                m_signature_help.hide();
              }
            });
      }

      Language::Protocol::Position pos{
          .line = doc->get_caret_line(),
          .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_completion(
          uri, fname, pos, doc->get_line(doc->get_caret_line()),
          [this](std::vector<Language::Protocol::CompletionItem> items) {
            std::lock_guard<std::mutex> lock(m_lsp_mutex);
            if (!items.empty()) {
              m_completion_popup.show(std::move(items), 100.0F, 100.0F);
            }
          });
      return true;
    }
  }

  const bool changed = m_controller.insert_text(utf8_text);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();

    const std::string uri = get_active_document_uri();
    const std::string fname = get_active_document_filename();
    std::string content;
    for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
      content += doc->get_line(i);
      content += "\n";
    }
    Language::LanguageServerManager::instance().on_document_changed(
        uri, fname, 1, content);

    const std::string_view current_line = doc->get_line(doc->get_caret_line());
    const std::size_t caret_col = doc->get_caret_column();

    std::string filter_word;
    const std::string_view line_to_caret =
        current_line.substr(0, std::min(caret_col, current_line.size()));
    const std::size_t inc_idx = line_to_caret.find("#include");
    const std::size_t imp_idx = line_to_caret.find("#import");
    if (inc_idx != std::string_view::npos ||
        imp_idx != std::string_view::npos) {
      const std::size_t last_lt = line_to_caret.rfind('<');
      const std::size_t last_q = line_to_caret.rfind('"');
      if (last_lt != std::string_view::npos &&
          (line_to_caret.rfind('>') == std::string_view::npos ||
           last_lt > line_to_caret.rfind('>'))) {
        filter_word = std::string(line_to_caret.substr(last_lt + 1));
      } else if (last_q != std::string_view::npos &&
                 (std::count(line_to_caret.begin(),
                             line_to_caret.end(), '"') % 2 == 1)) {
        filter_word = std::string(line_to_caret.substr(last_q + 1));
      }
    }
    if (filter_word.empty()) {
      std::size_t word_start = std::min(caret_col, current_line.size());
      while (word_start > 0) {
        const char c = current_line[word_start - 1];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
            c == '#' || c == ':' || c == '~') {
          --word_start;
        } else {
          break;
        }
      }
      filter_word =
          std::string(current_line.substr(word_start, caret_col - word_start));
    }

    const bool is_include_context =
        current_line.find('#') != std::string_view::npos ||
        utf8_text == "<" || utf8_text == "\"" || utf8_text == "#";
    const bool is_trigger_char =
        utf8_text == "." || utf8_text == ">" || utf8_text == ":" ||
        utf8_text == "/" || utf8_text == "\\" || utf8_text == "(" ||
        utf8_text == "," || is_include_context || filter_word.size() >= 1;

    if (utf8_text == "(" || utf8_text == ",") {
      Language::Protocol::Position sig_pos{
          .line = doc->get_caret_line(),
          .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_signature_help(
          uri, fname, sig_pos, current_line,
          [this](std::optional<Language::Protocol::SignatureHelp> help) {
            std::lock_guard<std::mutex> lock(m_lsp_mutex);
            if (help.has_value() && !help->signatures.empty()) {
              m_signature_help.show(std::move(*help), 0.0F, 0.0F);
            } else {
              m_signature_help.hide();
            }
          });
    }

    if (is_trigger_char) {
      Language::Protocol::Position pos{
          .line = doc->get_caret_line(),
          .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_completion(
          uri, fname, pos, current_line,
          [this, filter_word](std::vector<Language::Protocol::CompletionItem> items) {
            std::lock_guard<std::mutex> lock(m_lsp_mutex);
            if (!items.empty()) {
              m_completion_popup.show(std::move(items), 100.0F, 100.0F);
              m_completion_popup.set_filter(filter_word);
            }
          });
    }
  }
  return changed;
}

bool TextEditor::is_focused() const noexcept { return m_focused; }

bool TextEditor::is_empty_state_interactive_point(
    float point_x, float point_y) const noexcept {
  if (m_controller.get_active_document() != nullptr) {
    return false;
  }
  return m_empty_state_open_btn.get_bounds().contains(point_x, point_y) ||
         m_empty_state_clone_btn.get_bounds().contains(point_x, point_y);
}

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

bool TextEditor::is_fold_margin_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * surface.m_dpi_scale;
  const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
  return layout.gutter_bounds.contains(point_x, point_y) &&
         point_x >= fold_margin_left;
}

bool TextEditor::tick_animations() noexcept {
  bool needs_repaint = m_caret_blink.tick();
  needs_repaint |= m_selection_animation.tick();
  needs_repaint |= m_brace_animation.tick();

  const std::span<const UI::Editor::EditorSessionDocument> docs =
      m_controller.get_documents();
  for (const auto &doc : docs) {
    const auto *key = &doc.text;
    if (m_tab_animated_x.contains(key) && m_tab_target_x.contains(key)) {
      float current = m_tab_animated_x[key];
      const float target = m_tab_target_x[key];
      if (std::abs(current - target) > 0.5F) {
        current += (target - current) * 0.25F;
        m_tab_animated_x[key] = current;
        needs_repaint = true;
      } else {
        m_tab_animated_x[key] = target;
      }
    }
  }

  if (const auto *doc = m_controller.get_active_document()) {
    const UI::Editor::TextPosition caret{doc->get_caret_line(),
                                         doc->get_caret_column()};
    if (caret.line != m_last_brace_caret.line ||
        caret.column != m_last_brace_caret.column) {
      m_last_brace_caret = caret;
      auto [open_pos, close_pos] = find_enclosing_braces(*doc);
      if (open_pos.has_value() && close_pos.has_value()) {
        m_brace_animation.set_active_braces(open_pos, close_pos);
        needs_repaint = true;
      } else {
        m_brace_animation.clear();
      }
    }
  }

  return needs_repaint;
}

const UI::Editor::TextDocumentModel *TextEditor::get_document() const noexcept {
  return m_controller.get_active_document();
}

void TextEditor::render(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  draw_tab_strip(surface, drawable, layout);
  draw_document(surface, drawable, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const std::span<const UI::Editor::EditorSessionDocument> documents =
      m_controller.get_documents();
  m_tab_count = std::min(documents.size(), max_visible_tabs);

  surface.fill_rectangle(drawable, layout.tab_bar_bounds,
                         surface.m_pixels.tab_background);

  float total_tabs_width = 0.0F;
  for (std::size_t index = 0; index < m_tab_count; ++index) {
    const float tab_width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{documents[index].text.get_file_name()})),
        surface.m_dpi_scale);
    total_tabs_width += tab_width;
    if (index + 1 < m_tab_count) {
      total_tabs_width +=
          UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }
  }

  m_max_tab_scroll =
      std::max(0.0F, total_tabs_width - layout.tab_bar_bounds.width);
  if (m_max_tab_scroll == 0.0F) {
    m_tab_scroll_offset = 0.0F;
  } else if (m_tab_scroll_offset > m_max_tab_scroll) {
    m_tab_scroll_offset = m_max_tab_scroll;
  }

  float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
  const float tab_height = layout.tab_bar_bounds.height;
  const float close_button_width =
      UI::Editor::StudioEditorMetrics::editor_tab_close_width *
      surface.m_dpi_scale;

  surface.push_clip(layout.tab_bar_bounds);

  for (std::size_t index = 0; index < m_tab_count; ++index) {
    const auto &doc = documents[index];
    const float tab_width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{doc.text.get_file_name()})),
        surface.m_dpi_scale);

    UI::Rect tab_rect{tab_x, layout.tab_bar_bounds.y, tab_width, tab_height};
    if (m_tab_drag_drop.is_dragging() &&
        m_tab_drag_drop.get_dragged_index() == index) {
      tab_rect.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
    }
    m_tab_bounds[index] = tab_rect;
    tab_x += tab_width +
             UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
  }

  const auto active_index = m_controller.get_active_index();

  auto draw_single_tab = [&](std::size_t index) {
    const auto &doc = documents[index];
    const UI::Rect &tab_rect = m_tab_bounds[index];
    const bool is_active = (active_index.has_value() && *active_index == index);
    const bool is_hovered =
        m_hovered_tab_index && *m_hovered_tab_index == index;

    unsigned long tab_bg = surface.m_pixels.tab_background;
    if (is_active) {
      tab_bg = surface.m_pixels.editor_background;
    } else if (is_hovered) {
      tab_bg = surface.m_pixels.hover_background;
    }

    surface.fill_rectangle(drawable, tab_rect, tab_bg);

    const int tab_left = round_to_int(tab_rect.x);
    const int tab_right = round_to_int(tab_rect.right()) - 1;
    const int tab_top = round_to_int(tab_rect.y);
    const int tab_bottom = round_to_int(tab_rect.bottom()) - 1;

    // Clean top and vertical edge rules (no blue line), matching Win32
    surface.draw_line(drawable, tab_left, tab_top, tab_right, tab_top,
                      surface.m_pixels.border);
    surface.draw_line(drawable, tab_left, tab_top, tab_left, tab_bottom,
                      surface.m_pixels.border);
    surface.draw_line(drawable, tab_right, tab_top, tab_right, tab_bottom,
                      surface.m_pixels.border);

    const float icon_x = tab_rect.x + 8.0F * surface.m_dpi_scale;
    const float icon_y = tab_rect.y + tab_rect.height * 0.5F;
    const int icon_size = std::max(round_to_int(14.0F * surface.m_dpi_scale), 11);

    const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
        std::filesystem::path{std::string{doc.text.get_file_name()}});
    surface.draw_svg_icon(
        drawable,
        icon_asset,
        round_to_int(icon_x + icon_size * 0.5F),
        round_to_int(icon_y),
        icon_size,
        surface.m_palette.text_primary,
        is_active ? surface.m_palette.editor_background
                  : (is_hovered ? surface.m_palette.hover_background
                                : surface.m_palette.tab_background),
        true);

    const float text_x = icon_x + 18.0F * surface.m_dpi_scale;
    const float text_y = tab_rect.y + tab_rect.height * 0.5F;

    surface.draw_text(
        drawable, *surface.m_ui_font, doc.text.get_file_name(), text_x,
        text_y, is_active ? surface.m_text.primary : surface.m_text.muted,
        &layout.tab_bar_bounds);

    const UI::Rect close_rect{tab_rect.right() - close_button_width,
                              tab_rect.y, close_button_width, tab_height};
    const bool close_hovered =
        m_hovered_tab_close_index && *m_hovered_tab_close_index == index;

    const int close_cx = round_to_int(close_rect.x + close_rect.width * 0.5F);
    const int close_cy = round_to_int(close_rect.y + close_rect.height * 0.5F);
    const int cross_size = round_to_int(3.5F * surface.m_dpi_scale);

    const unsigned long cross_color =
        close_hovered ? surface.m_pixels.text_primary
                      : surface.m_pixels.text_muted;
    surface.draw_line(drawable, close_cx - cross_size,
                      close_cy - cross_size, close_cx + cross_size,
                      close_cy + cross_size, cross_color);
    surface.draw_line(drawable, close_cx - cross_size,
                      close_cy + cross_size, close_cx + cross_size,
                      close_cy - cross_size, cross_color);
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

  surface.pop_clip();
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
    const float full_height =
        layout.editor_bounds.height + layout.terminal_panel_bounds.height;

    int current_y = round_to_int(layout.editor_bounds.y +
                                 (full_height - total_height) * 0.5F);

    surface.draw_png_icon(drawable, "zenvra_logo.png", center_x,
                          current_y + logo_size / 2, logo_size,
                          surface.m_palette.editor_background);
    current_y += logo_size + gap1;

    if (surface.m_ui_font) {
      const std::string title = "Zenvra Development Studio";
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

      m_empty_state_open_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      const auto &open_state = m_empty_state_open_btn.get_state();
      const unsigned long open_bg =
          open_state.pressed ? surface.m_pixels.accent
                             : (open_state.hovered ? surface.m_pixels.accent
                                                   : surface.m_pixels.accent);

      surface.fill_rounded_rectangle(
          drawable, m_empty_state_open_btn.get_bounds(), open_bg, 4.0F * dpi,
          surface.m_pixels.editor_background);

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

      m_empty_state_clone_btn.set_bounds(
          UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
      const auto &clone_state = m_empty_state_clone_btn.get_state();
      const UI::Theme::Color &clone_icon_bg =
          clone_state.pressed
              ? surface.m_palette.border
              : (clone_state.hovered ? surface.m_palette.hover_background
                                     : surface.m_palette.editor_background);
      const unsigned long clone_bg = surface.allocate_color(clone_icon_bg);
      surface.fill_rounded_rectangle(
          drawable, m_empty_state_clone_btn.get_bounds(), clone_bg, 4.0F * dpi,
          surface.m_pixels.editor_background);

      const float clone_text_w = static_cast<float>(
          surface.m_ui_font->getTextWidth("Clone Repository"));
      std::array<Utility::FlexItem, 2> flex_items_clone{
          Utility::FlexItem::fixed(icon_size),
          Utility::FlexItem::fixed(clone_text_w)};

      Utility::FlexLayoutResult flex_res_clone = Utility::Flex::calculate(
          m_empty_state_clone_btn.get_bounds(), flex_items_clone, flex_opts);

      surface.draw_svg_icon(drawable, "Assets/icons/copy.svg",
                            round_to_int(flex_res_clone.items[0].x +
                                         flex_res_clone.items[0].width * 0.5F),
                            round_to_int(flex_res_clone.items[0].y +
                                         flex_res_clone.items[0].height * 0.5F),
                            round_to_int(icon_size),
                            surface.m_palette.text_primary, clone_icon_bg);

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
  const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale -
                       m_text_scroll_offset;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines = document->get_line_count();

  const std::size_t tab_size = document->get_status().indent_width > 0
                                   ? document->get_status().indent_width
                                   : 4;

  m_folding.rebuild(std::vector<std::string>(document->get_lines().begin(),
                                             document->get_lines().end()),
                    tab_size);

  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);
  if (m_reveal_caret_pending) {
    static_cast<void>(m_scrollbar.reveal_line(physical_line_to_visual_row(
        m_folding, document->get_caret_line(), total_lines)));
    m_reveal_caret_pending = false;
  }

  const std::size_t first_line = visual_row_to_physical_line(
      m_folding, m_scrollbar.get_first_visible_line(), total_lines);
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

  // Indent guides
  {
    const float space_width =
        static_cast<float>(surface.m_editor_font->getTextWidth(" "));
    const UI::Components::ActiveIndentScope active_scope =
        m_folding.get_active_indent_scope(document->get_caret_line(), tab_size);

    std::size_t row_guide = 0;
    for (std::size_t line_index = first_line;
         row_guide < render_count && line_index < total_lines; ++line_index) {
      if (m_folding.is_line_hidden(line_index)) {
        continue;
      }
      const float center_y =
          first_center_y + static_cast<float>(row_guide) * line_height;
      ++row_guide;

      const std::size_t line_indent =
          m_folding.get_effective_indent(line_index);
      if (line_indent < tab_size) {
        continue;
      }

      const float y_top = center_y - line_height * 0.5F;
      const float y_bottom = center_y + line_height * 0.5F;

      for (std::size_t col = tab_size; col <= line_indent; col += tab_size) {
        const float guide_x = code_x + static_cast<float>(col) * space_width;
        if (guide_x < layout.editor_bounds.x ||
            guide_x > layout.editor_bounds.right()) {
          continue;
        }

        const bool is_active = active_scope.valid &&
                               col == active_scope.column &&
                               line_index >= active_scope.start_line &&
                               line_index <= active_scope.end_line;

        surface.draw_line(drawable, round_to_int(guide_x), round_to_int(y_top),
                          round_to_int(guide_x), round_to_int(y_bottom),
                          is_active ? surface.m_pixels.indent_guide_active
                                    : surface.m_pixels.indent_guide);
      }
    }
  }

  const float hscroll_height =
      (m_max_text_scroll > 0.0F) ? 14.0F * surface.m_dpi_scale : 0.0F;
  UI::Rect text_clip_rect = layout.editor_bounds;
  text_clip_rect.height =
      std::max(0.0F, text_clip_rect.height - hscroll_height);

  float max_line_width = 0.0F;

  // Pass 1: Gutter and line numbers
  std::size_t row_pass1 = 0;
  for (std::size_t line_index = first_line;
       row_pass1 < render_count && line_index < total_lines; ++line_index) {
    if (m_folding.is_line_hidden(line_index)) {
      continue;
    }
    const std::string_view line = document->get_line(line_index);
    const float center_y =
        first_center_y + static_cast<float>(row_pass1) * line_height;
    ++row_pass1;
    const bool active_line = line_index == document->get_caret_line();
    if (active_line && !document->has_selection()) {
      surface.fill_rectangle(
          drawable,
          UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                   layout.editor_bounds.right() - layout.gutter_bounds.x,
                   line_height},
          surface.m_pixels.active_line_background);
    }
    const std::string number = std::to_string(line_index + 1);
    const float number_x =
        layout.gutter_bounds.right() - fold_margin - 4.0F * surface.m_dpi_scale -
        static_cast<float>(surface.m_small_font->getTextWidth(number));
    surface.draw_text(
        drawable, *surface.m_small_font, number, number_x, center_y,
        active_line ? surface.m_text.primary : surface.m_text.muted);

    const auto gutter_diags = document->get_diagnostics_for_line(line_index);
    if (!gutter_diags.empty()) {
      bool has_error = false;
      bool has_warn = false;
      for (const auto &gd : gutter_diags) {
        if (gd.severity == Language::Protocol::DiagnosticSeverity::Error)
          has_error = true;
        else if (gd.severity == Language::Protocol::DiagnosticSeverity::Warning)
          has_warn = true;
      }

      const float dot_x = layout.gutter_bounds.x + 4.0F * surface.m_dpi_scale;
      const float dot_r = 3.0F * surface.m_dpi_scale;
      const UI::Theme::Color dot_color =
          has_error
              ? UI::Theme::Color{247, 84, 100, 255}
              : (has_warn ? UI::Theme::Color{240, 167, 50, 255}
                          : UI::Theme::Color{86, 182, 194, 255});
      surface.fill_rounded_rectangle(
          drawable,
          UI::Rect{dot_x, center_y - dot_r, dot_r * 2.0F, dot_r * 2.0F},
          surface.allocate_color(dot_color), dot_r);
    }
    if (has_gutter_marker(line)) {
      const int marker_x = round_to_int(
          layout.gutter_bounds.right() - 13.0F * surface.m_dpi_scale);
      const int marker_y = round_to_int(center_y);
      const int half = std::max(round_to_int(3.0F * surface.m_dpi_scale), 2);
      surface.draw_line(drawable, marker_x, marker_y - half, marker_x + half,
                        marker_y, surface.m_pixels.text_muted);
      surface.draw_line(drawable, marker_x + half, marker_y, marker_x,
                        marker_y + half, surface.m_pixels.text_muted);
      surface.draw_line(drawable, marker_x, marker_y + half, marker_x - half,
                        marker_y, surface.m_pixels.text_muted);
      surface.draw_line(drawable, marker_x - half, marker_y, marker_x,
                        marker_y - half, surface.m_pixels.text_muted);
    }

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

      surface.fill_rectangle(
          drawable,
          UI::Rect{static_cast<float>(fold_cx - box_half),
                   static_cast<float>(fold_cy - box_half),
                   static_cast<float>(box_half * 2),
                   static_cast<float>(box_half * 2)},
          active_line ? surface.m_pixels.active_line_background
                      : surface.m_pixels.editor_background);

      surface.draw_rectangle(
          drawable,
          UI::Rect{static_cast<float>(fold_cx - box_half),
                   static_cast<float>(fold_cy - box_half),
                   static_cast<float>(box_half * 2),
                   static_cast<float>(box_half * 2)},
          fold_hovered ? surface.m_pixels.accent : surface.m_pixels.border);

      const int sign_inset =
          std::max(round_to_int(2.0F * surface.m_dpi_scale), 2);
      surface.draw_line(drawable, fold_cx - box_half + sign_inset, fold_cy,
                        fold_cx + box_half - sign_inset, fold_cy,
                        fold_hovered ? surface.m_pixels.accent
                                     : surface.m_pixels.text_muted);

      if (fold_marker == UI::Components::FoldMarker::Collapsed) {
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

    if (fold_marker == UI::Components::FoldMarker::Expanded) {
      const int box_half =
          std::max(round_to_int(5.0F * surface.m_dpi_scale), 4);
      surface.draw_line(drawable, fold_cx, fold_cy + box_half, fold_cx,
                        round_to_int(center_y + line_height * 0.5F),
                        surface.m_pixels.border);
    }
  }

  // Pass 2: Selection & Text
  surface.push_clip(text_clip_rect);

  std::vector<UI::Rect> selection_targets;
  for (const auto &cursor : document->get_all_cursors()) {
    if (cursor.has_selection()) {
      const UI::Editor::TextSelection selection = cursor.get_selection();
      const std::size_t start_line = selection.start.line;
      const std::size_t end_line =
          std::min(selection.end.line, start_line + 1000);

      for (std::size_t line_index = start_line; line_index <= end_line;
           ++line_index) {
        if (m_folding.is_line_hidden(line_index)) {
          continue;
        }
        const std::string_view line = document->get_line(line_index);
        const std::size_t selection_start =
            line_index == selection.start.line ? selection.start.column : 0;
        const std::size_t selection_end =
            line_index == selection.end.line ? selection.end.column : line.size();

        const float selection_x = static_cast<float>(surface.m_editor_font->getTextWidth(
            std::string{line.substr(0, selection_start)}));
        float selection_width = static_cast<float>(surface.m_editor_font->getTextWidth(
            std::string{line.substr(selection_start, selection_end - selection_start)}));

        if (line_index < selection.end.line) {
          selection_width += 6.0F * surface.m_dpi_scale;
        }

        selection_targets.push_back(UI::Rect{
            selection_x,
            static_cast<float>(physical_line_to_visual_row(
                m_folding, line_index, total_lines)) *
                line_height,
            selection_width, line_height});
      }
    }
  }

  if (selection_targets.empty()) {
    m_selection_animation.clear();
  } else {
    m_selection_animation.set_targets(selection_targets);
  }

  if (m_selection_animation.has_rects()) {
    const std::size_t first_visual_row = m_scrollbar.get_first_visible_line();
    for (const UI::Rect &anim_rect :
         m_selection_animation.get_animated_rects()) {
      if (anim_rect.width <= 0.0F)
        continue;

      const float screen_y = layout.editor_bounds.y + anim_rect.y -
                             static_cast<float>(first_visual_row) * line_height;
      const float screen_x = code_x + anim_rect.x;

      if (screen_y + anim_rect.height >= layout.editor_bounds.y &&
          screen_y <= layout.editor_bounds.bottom()) {
        const int snap_y = round_to_int(screen_y);
        const int snap_bottom = round_to_int(screen_y + anim_rect.height);
        const int snap_x = round_to_int(screen_x);
        const int snap_right = round_to_int(screen_x + anim_rect.width);

        surface.fill_rounded_rectangle(
            drawable,
            UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y),
                     static_cast<float>(snap_right - snap_x),
                     static_cast<float>(snap_bottom - snap_y)},
            surface.m_pixels.selection_background, 4.0F * surface.m_dpi_scale,
            surface.m_pixels.editor_background);
      }
    }
  }

  std::size_t row_pass2 = 0;
  for (std::size_t line_index = first_line;
       row_pass2 < render_count && line_index < total_lines; ++line_index) {
    if (m_folding.is_line_hidden(line_index)) {
      continue;
    }
    const std::string_view line = document->get_line(line_index);
    if (const float current_line_width = static_cast<float>(
            surface.m_editor_font->getTextWidth(std::string{line}));
        current_line_width > max_line_width) {
      max_line_width = current_line_width;
    }
    const float center_y =
        first_center_y + static_cast<float>(row_pass2) * line_height;
    ++row_pass2;

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
                              center_y, token_color(token.kind),
                              &text_clip_rect);
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

          surface.draw_text(drawable, *surface.m_editor_font, brace_char,
                            token_x, center_y, surface.m_text.accent,
                            &text_clip_rect);
          token_x += brace_w;

          if (brace_offset + 1 < token.text.size()) {
            const std::string post{token.text.substr(brace_offset + 1)};
            surface.draw_text(drawable, *surface.m_editor_font, post, token_x,
                              center_y, token_color(token.kind),
                              &text_clip_rect);
            token_x +=
                static_cast<float>(surface.m_editor_font->getTextWidth(post));
          }
        } else {
          const std::string text{token.text};
          surface.draw_text(drawable, *surface.m_editor_font, text, token_x,
                            center_y, token_color(token.kind), &text_clip_rect);
          token_x +=
              static_cast<float>(surface.m_editor_font->getTextWidth(text));
        }
        rendered_bytes += token.text.size();
      }
      if (rendered_bytes < line.size()) {
        surface.draw_text(drawable, *surface.m_editor_font,
                          line.substr(rendered_bytes), token_x, center_y,
                          surface.m_text.primary, &text_clip_rect);
      }
    } else {
      surface.draw_text(drawable, *surface.m_editor_font, line, code_x,
                        center_y, surface.m_text.primary, &text_clip_rect);
    }

    // Diagnostics Squiggles
    const auto &diags = document->get_diagnostics();
    for (const auto &diag : diags) {
      if (diag.range.start.line == line_index) {
        const std::size_t start_col =
            std::min<std::size_t>(diag.range.start.character, line.size());
        const std::size_t end_col =
            (diag.range.end.line == line_index)
                ? std::min<std::size_t>(diag.range.end.character, line.size())
                : line.size();
        const std::size_t sq_len =
            (end_col > start_col) ? (end_col - start_col) : 1;

        const float sq_x =
            code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                         std::string{line.substr(0, start_col)}));
        const float sq_w =
            static_cast<float>(surface.m_editor_font->getTextWidth(
                std::string{line.substr(start_col, sq_len)}));
        const float sq_y = center_y + line_height * 0.5F - 2.0F;

        unsigned long diag_color = surface.m_pixels.warning;
        if (diag.severity == Language::Protocol::DiagnosticSeverity::Error) {
          diag_color =
              surface.allocate_color(UI::Theme::Color{241, 76, 76, 255});
        } else if (diag.severity ==
                   Language::Protocol::DiagnosticSeverity::Warning) {
          diag_color =
              surface.allocate_color(UI::Theme::Color{204, 167, 0, 255});
        } else {
          diag_color =
              surface.allocate_color(UI::Theme::Color{117, 190, 255, 255});
        }

        float cur_x = sq_x;
        const float step = 3.0F * surface.m_dpi_scale;
        bool up = false;
        while (cur_x < sq_x + sq_w) {
          const float next_x = std::min(cur_x + step, sq_x + sq_w);
          const float y1 = up ? (sq_y - 1.5F) : (sq_y + 1.5F);
          const float y2 = up ? (sq_y + 1.5F) : (sq_y - 1.5F);
          surface.draw_line(drawable, round_to_int(cur_x), round_to_int(y1),
                            round_to_int(next_x), round_to_int(y2),
                            diag_color);
          cur_x = next_x;
          up = !up;
        }
      }
    }

    // Render JetBrains-style Inline Error Lens (Inspection hint at end of line)
    const auto line_diags = document->get_diagnostics_for_line(line_index);
    if (!line_diags.empty()) {
      const auto *top_diag = &line_diags[0];
      for (const auto &d : line_diags) {
        if (d.severity < top_diag->severity) {
          top_diag = &d;
        }
      }

      std::string badge_prefix = "   x  ";
      std::string text_color = "#f75464";
      UI::Theme::Color badge_bg{48, 20, 24, 180};
      if (top_diag->severity ==
          Language::Protocol::DiagnosticSeverity::Warning) {
        badge_prefix = "   !  ";
        text_color = "#f0a732";
        badge_bg = UI::Theme::Color{48, 38, 20, 180};
      } else if (top_diag->severity >=
                 Language::Protocol::DiagnosticSeverity::Information) {
        badge_prefix = "   i  ";
        text_color = "#56b6c2";
        badge_bg = UI::Theme::Color{20, 36, 48, 180};
      }

      std::string hint_text = badge_prefix + top_diag->message;
      if (hint_text.size() > 90) {
        hint_text = hint_text.substr(0, 87) + "...";
      }

      const float current_line_width = static_cast<float>(
          surface.m_editor_font->getTextWidth(std::string{line}));
      const float hint_x =
          code_x + current_line_width + 20.0F * surface.m_dpi_scale;
      const int hint_w = surface.m_ui_font->getTextWidth(hint_text);
      const float badge_h = 16.0F * surface.m_dpi_scale;

      surface.fill_rounded_rectangle(
          drawable,
          UI::Rect{hint_x - 4.0F * surface.m_dpi_scale,
                   center_y - badge_h * 0.5F,
                   static_cast<float>(hint_w) + 8.0F * surface.m_dpi_scale,
                   badge_h},
          surface.allocate_color(badge_bg), 3.0F * surface.m_dpi_scale,
          surface.m_pixels.editor_background);

      surface.draw_text(drawable, *surface.m_ui_font, hint_text, hint_x,
                        center_y, text_color, &text_clip_rect);
    }

    // Caret
    if (m_focused && m_caret_blink.is_visible()) {
      for (const auto &cur : document->get_all_cursors()) {
        if (cur.line == line_index) {
          const std::string_view prefix =
              line.substr(0, std::min(cur.column, line.size()));
          const int caret_x = round_to_int(
              code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                           std::string{prefix})));
          surface.draw_line(
              drawable, caret_x,
              round_to_int(center_y - 8.0F * surface.m_dpi_scale), caret_x,
              round_to_int(center_y + 8.0F * surface.m_dpi_scale),
              surface.m_pixels.text_primary);
        }
      }
    }
  }

  surface.pop_clip();

  // Horizontal scrollbar
  const float content_width =
      max_line_width + 28.0F * surface.m_dpi_scale;
  const float new_max_scroll =
      std::max(0.0F, content_width - layout.editor_bounds.width);
  if (new_max_scroll > m_max_text_scroll) {
    m_max_text_scroll = new_max_scroll;
  } else if (new_max_scroll < m_max_text_scroll * 0.8F) {
    m_max_text_scroll = new_max_scroll;
  }

  if (m_max_text_scroll == 0.0F) {
    const_cast<TextEditor *>(this)->m_text_scroll_offset = 0.0F;
  } else if (m_text_scroll_offset > m_max_text_scroll) {
    const_cast<TextEditor *>(this)->m_text_scroll_offset = m_max_text_scroll;
  }

  if (m_max_text_scroll > 0.0F) {
    const float track_width = layout.editor_bounds.width;
    const float track_height = 14.0F * surface.m_dpi_scale;
    const float track_y = layout.editor_bounds.bottom() - track_height;
    const float thumb_width =
        std::max(20.0F * surface.m_dpi_scale,
                 track_width * (track_width / content_width));
    const float thumb_x =
        layout.editor_bounds.x + (m_text_scroll_offset / m_max_text_scroll) *
                                     (track_width - thumb_width);
    const float thumb_height = 6.0F * surface.m_dpi_scale;
    const UI::Rect thumb_bounds{thumb_x,
                                track_y + (track_height - thumb_height) * 0.5F,
                                thumb_width, thumb_height};
    surface.fill_rectangle(drawable, thumb_bounds, surface.m_pixels.text_muted);
  }

  // --- IntelliSense Completion Popup Overlay ---
  std::lock_guard<std::mutex> lsp_lock(m_lsp_mutex);
  if (m_completion_popup.is_visible() &&
      m_completion_popup.get_item_count() > 0 && document != nullptr) {
    const std::string_view current_line =
        document->get_line(document->get_caret_line());
    const std::string_view prefix = current_line.substr(
        0, std::min(document->get_caret_column(), current_line.size()));
    const float caret_screen_x =
        code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                     std::string{prefix}));
    const float caret_line_y =
        layout.editor_bounds.y +
        static_cast<float>(physical_line_to_visual_row(
                               m_folding, document->get_caret_line(),
                               document->get_line_count()) -
                           m_scrollbar.get_first_visible_line() + 1) *
            (20.0F * surface.m_dpi_scale);

    const float item_h = 20.0F * surface.m_dpi_scale;
    const std::size_t count = m_completion_popup.get_item_count();
    const std::size_t scroll_offset = m_completion_popup.get_scroll_offset();
    const std::size_t max_visible = m_completion_popup.get_max_visible_items();
    const std::size_t visible_items = std::min<std::size_t>(count, max_visible);
    const float popup_h =
        static_cast<float>(visible_items) * item_h + 4.0F * surface.m_dpi_scale;

    float max_label_w = 220.0F * surface.m_dpi_scale;
    for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count;
         ++i) {
      if (const auto *it = m_completion_popup.get_item(scroll_offset + i)) {
        const int w = surface.m_ui_font->getTextWidth(it->label);
        max_label_w = std::max(max_label_w, static_cast<float>(w) +
                                                64.0F * surface.m_dpi_scale);
      }
    }
    const float popup_w = std::clamp(
        max_label_w, 220.0F * surface.m_dpi_scale, 380.0F * surface.m_dpi_scale);

    const float popup_x = std::clamp(
        caret_screen_x, layout.editor_bounds.x + 10.0F,
        std::max(layout.editor_bounds.x + 10.0F,
                 layout.editor_bounds.right() - (popup_w + 20.0F)));
    const float popup_y = std::clamp(
        caret_line_y, layout.editor_bounds.y + 10.0F,
        std::max(layout.editor_bounds.y + 10.0F,
                 layout.editor_bounds.bottom() - (popup_h + 20.0F)));

    const UI::Rect actual_bounds{popup_x, popup_y, popup_w, popup_h};

    const UI::Theme::Color vscode_bg{24, 24, 28, 255};
    const UI::Theme::Color vscode_border{55, 55, 62, 255};
    const UI::Theme::Color vscode_selection{0, 95, 184, 255};
    const UI::Theme::Color vscode_text_muted{133, 133, 133, 255};

    surface.fill_rounded_rectangle(
        drawable, actual_bounds, surface.allocate_color(vscode_bg),
        3.0F * surface.m_dpi_scale, surface.m_pixels.editor_background);
    surface.draw_rectangle(drawable, actual_bounds,
                           surface.allocate_color(vscode_border));

    const std::size_t selected = m_completion_popup.get_selected_index();

    for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count;
         ++i) {
      const std::size_t item_idx = scroll_offset + i;
      const auto *item = m_completion_popup.get_item(item_idx);
      if (item == nullptr)
        continue;

      const float row_y =
          actual_bounds.y + 2.0F + static_cast<float>(i) * item_h;
      const float item_w = actual_bounds.width -
                           (count > max_visible ? 10.0F : 4.0F) *
                               surface.m_dpi_scale;
      const UI::Rect item_rect{actual_bounds.x + 2.0F, row_y, item_w, item_h};

      if (item_idx == selected) {
        surface.fill_rounded_rectangle(
            drawable, item_rect, surface.allocate_color(vscode_selection),
            2.0F * surface.m_dpi_scale, surface.allocate_color(vscode_bg));
      }

      std::string kind_badge = " ";
      UI::Theme::Color badge_color = surface.m_palette.accent;
      bool is_snippet = false;

      switch (item->kind) {
      case Language::Protocol::CompletionItemKind::Snippet:
        kind_badge = "[]";
        badge_color = UI::Theme::Color{79, 193, 255, 255};
        is_snippet = true;
        break;
      case Language::Protocol::CompletionItemKind::Keyword:
        kind_badge = "{}";
        badge_color = UI::Theme::Color{197, 134, 192, 255};
        break;
      case Language::Protocol::CompletionItemKind::Function:
      case Language::Protocol::CompletionItemKind::Method:
        kind_badge = "f";
        badge_color = UI::Theme::Color{177, 128, 215, 255};
        break;
      case Language::Protocol::CompletionItemKind::Variable:
      case Language::Protocol::CompletionItemKind::Field:
        kind_badge = "v";
        badge_color = UI::Theme::Color{156, 220, 254, 255};
        break;
      case Language::Protocol::CompletionItemKind::Property:
        kind_badge = "p";
        badge_color = UI::Theme::Color{79, 193, 255, 255};
        break;
      case Language::Protocol::CompletionItemKind::Class:
      case Language::Protocol::CompletionItemKind::Struct:
      case Language::Protocol::CompletionItemKind::Interface:
        kind_badge = "c";
        badge_color = UI::Theme::Color{78, 201, 176, 255};
        break;
      case Language::Protocol::CompletionItemKind::File:
        kind_badge = "h";
        badge_color = UI::Theme::Color{156, 220, 254, 255};
        break;
      case Language::Protocol::CompletionItemKind::Module:
        kind_badge = "m";
        badge_color = UI::Theme::Color{220, 220, 170, 255};
        break;
      default:
        kind_badge = "abc";
        badge_color = surface.m_palette.text_muted;
        break;
      }

      surface.draw_text(drawable, *surface.m_ui_font, kind_badge,
                        item_rect.x + 6.0F * surface.m_dpi_scale,
                        row_y + item_h * 0.5F, badge_color);

      UI::Theme::Color label_color = surface.m_palette.text_primary;
      if (item_idx == selected) {
        label_color = UI::Theme::Color{255, 255, 255, 255};
      } else if (is_snippet) {
        label_color = UI::Theme::Color{79, 193, 255, 255};
      }
      surface.draw_text(drawable, *surface.m_ui_font, item->label,
                        item_rect.x + 28.0F * surface.m_dpi_scale,
                        row_y + item_h * 0.5F, label_color);

      if (is_snippet) {
        surface.draw_text(drawable, *surface.m_ui_font, "<-",
                          item_rect.right() - 18.0F * surface.m_dpi_scale,
                          row_y + item_h * 0.5F,
                          (item_idx == selected)
                              ? UI::Theme::Color{255, 255, 255, 255}
                              : vscode_text_muted);
      } else if (!item->detail.empty()) {
        const int detail_w = surface.m_ui_font->getTextWidth(item->detail);
        const float detail_x =
            std::max(item_rect.x + 180.0F * surface.m_dpi_scale,
                     item_rect.right() - static_cast<float>(detail_w) -
                         6.0F * surface.m_dpi_scale);
        surface.draw_text(drawable, *surface.m_ui_font, item->detail, detail_x,
                          row_y + item_h * 0.5F, vscode_text_muted);
      }
    }

    // Scrollbar thumb for popup
    if (count > max_visible) {
      const float track_x = actual_bounds.right() - 4.0F * surface.m_dpi_scale;
      const float track_y = actual_bounds.y + 2.0F;
      const float track_h = static_cast<float>(visible_items) * item_h;
      const float thumb_h = std::max(
          12.0F * surface.m_dpi_scale,
          track_h * (static_cast<float>(max_visible) / static_cast<float>(count)));
      const float max_scroll = static_cast<float>(count - max_visible);
      const float thumb_y =
          track_y +
          (static_cast<float>(scroll_offset) / max_scroll) * (track_h - thumb_h);

      const UI::Rect thumb_rect{track_x, thumb_y, 3.0F * surface.m_dpi_scale,
                                thumb_h};
      surface.fill_rounded_rectangle(
          drawable, thumb_rect,
          surface.allocate_color(UI::Theme::Color{90, 90, 96, 255}),
          1.5F * surface.m_dpi_scale, surface.allocate_color(vscode_bg));
    }

    // Flyout documentation card
    const auto *selected_item = m_completion_popup.get_selected_item();
    if (selected_item != nullptr &&
        (!selected_item->detail.empty() ||
         !selected_item->documentation.empty() ||
         selected_item->kind !=
             Language::Protocol::CompletionItemKind::Text)) {
      const float detail_pad = 8.0F * surface.m_dpi_scale;
      const float detail_w = 340.0F * surface.m_dpi_scale;

      float detail_x = actual_bounds.right() + 4.0F * surface.m_dpi_scale;
      if (detail_x + detail_w > layout.editor_bounds.right() - 8.0F) {
        detail_x = actual_bounds.x - detail_w - 4.0F * surface.m_dpi_scale;
        if (detail_x < layout.editor_bounds.x + 8.0F) {
          detail_x = std::max(layout.editor_bounds.x + 8.0F,
                              layout.editor_bounds.right() - detail_w - 8.0F);
        }
      }

      std::vector<std::string> doc_lines;
      if (!selected_item->documentation.empty()) {
        std::stringstream ss(selected_item->documentation);
        std::string line;
        while (std::getline(ss, line)) {
          if (line.empty()) {
            doc_lines.push_back("");
            continue;
          }
          std::stringstream words_ss(line);
          std::string word;
          std::string current_wrapped;
          while (words_ss >> word) {
            std::string test =
                current_wrapped.empty() ? word : current_wrapped + " " + word;
            int text_w = surface.m_ui_font->getTextWidth(test);
            if (static_cast<float>(text_w) >
                    (detail_w - detail_pad * 2.0F - 4.0F) &&
                !current_wrapped.empty()) {
              doc_lines.push_back(current_wrapped);
              current_wrapped = word;
            } else {
              current_wrapped = test;
            }
          }
          if (!current_wrapped.empty()) {
            doc_lines.push_back(current_wrapped);
          }
        }
      }

      const float header_h = 24.0F * surface.m_dpi_scale;
      const float line_spacing = 16.0F * surface.m_dpi_scale;
      const float content_h =
          header_h + (doc_lines.empty()
                          ? 6.0F * surface.m_dpi_scale
                          : (6.0F * surface.m_dpi_scale +
                             static_cast<float>(doc_lines.size()) *
                                 line_spacing +
                             detail_pad));
      const float detail_h = std::clamp(
          std::max(actual_bounds.height, content_h),
          48.0F * surface.m_dpi_scale, 300.0F * surface.m_dpi_scale);
      const float detail_y = actual_bounds.y;

      const UI::Rect detail_bounds{detail_x, detail_y, detail_w, detail_h};

      surface.fill_rounded_rectangle(
          drawable, detail_bounds,
          surface.allocate_color(UI::Theme::Color{22, 22, 26, 255}),
          3.0F * surface.m_dpi_scale, surface.m_pixels.editor_background);
      surface.draw_rectangle(drawable, detail_bounds,
                             surface.allocate_color(vscode_border));

      float cursor_x = detail_bounds.x + detail_pad;
      const float cursor_y = detail_bounds.y + header_h * 0.5F;

      std::string header_kind_badge = " ";
      UI::Theme::Color header_badge_color = surface.m_palette.accent;
      std::string kind_name;

      switch (selected_item->kind) {
      case Language::Protocol::CompletionItemKind::Snippet:
        header_kind_badge = "[]";
        header_badge_color = UI::Theme::Color{79, 193, 255, 255};
        kind_name = "(snippet)";
        break;
      case Language::Protocol::CompletionItemKind::Keyword:
        header_kind_badge = "{}";
        header_badge_color = UI::Theme::Color{197, 134, 192, 255};
        kind_name = "(keyword)";
        break;
      case Language::Protocol::CompletionItemKind::Function:
      case Language::Protocol::CompletionItemKind::Method:
        header_kind_badge = "f";
        header_badge_color = UI::Theme::Color{177, 128, 215, 255};
        kind_name = "(function)";
        break;
      case Language::Protocol::CompletionItemKind::Variable:
      case Language::Protocol::CompletionItemKind::Field:
        header_kind_badge = "v";
        header_badge_color = UI::Theme::Color{156, 220, 254, 255};
        kind_name = "(variable)";
        break;
      case Language::Protocol::CompletionItemKind::Property:
        header_kind_badge = "p";
        header_badge_color = UI::Theme::Color{79, 193, 255, 255};
        kind_name = "(property)";
        break;
      case Language::Protocol::CompletionItemKind::Class:
      case Language::Protocol::CompletionItemKind::Struct:
      case Language::Protocol::CompletionItemKind::Interface:
        header_kind_badge = "c";
        header_badge_color = UI::Theme::Color{78, 201, 176, 255};
        kind_name = "(type)";
        break;
      case Language::Protocol::CompletionItemKind::File:
        header_kind_badge = "h";
        header_badge_color = UI::Theme::Color{156, 220, 254, 255};
        kind_name = "(header)";
        break;
      case Language::Protocol::CompletionItemKind::Module:
        header_kind_badge = "m";
        header_badge_color = UI::Theme::Color{220, 220, 170, 255};
        kind_name = "(module)";
        break;
      default:
        header_kind_badge = "abc";
        header_badge_color = surface.m_palette.text_muted;
        kind_name = "";
        break;
      }

      surface.draw_text(drawable, *surface.m_ui_font, header_kind_badge,
                        cursor_x, cursor_y, header_badge_color);
      cursor_x += 20.0F * surface.m_dpi_scale;

      std::string header_text = selected_item->detail.empty()
                                    ? (kind_name + " " + selected_item->label)
                                    : selected_item->detail;
      surface.draw_text(drawable, *surface.m_editor_font, header_text,
                        cursor_x, cursor_y,
                        UI::Theme::Color{230, 230, 235, 255});

      const float sep_y = detail_bounds.y + header_h;
      surface.draw_line(drawable, round_to_int(detail_bounds.x),
                        round_to_int(sep_y),
                        round_to_int(detail_bounds.right()),
                        round_to_int(sep_y),
                        surface.allocate_color(vscode_border));

      float doc_y = sep_y + 10.0F * surface.m_dpi_scale;
      bool inside_code_block = false;
      for (const auto &doc_line : doc_lines) {
        if (doc_y + line_spacing * 0.5F > detail_bounds.bottom() - 4.0F)
          break;
        if (doc_line.starts_with("```")) {
          inside_code_block = !inside_code_block;
          continue;
        }
        if (!doc_line.empty()) {
          if (doc_line.starts_with("### ")) {
            surface.draw_text(drawable, *surface.m_ui_font, doc_line.substr(4),
                              detail_bounds.x + detail_pad, doc_y,
                              UI::Theme::Color{156, 220, 254, 255});
          } else if (inside_code_block) {
            surface.draw_text(
                drawable, *surface.m_editor_font, doc_line,
                detail_bounds.x + detail_pad + 6.0F * surface.m_dpi_scale,
                doc_y, UI::Theme::Color{230, 230, 240, 255});
          } else if (doc_line.starts_with("- ")) {
            surface.draw_text(
                drawable, *surface.m_ui_font, "• " + doc_line.substr(2),
                detail_bounds.x + detail_pad + 4.0F * surface.m_dpi_scale,
                doc_y, UI::Theme::Color{210, 210, 215, 255});
          } else {
            surface.draw_text(drawable, *surface.m_ui_font, doc_line,
                              detail_bounds.x + detail_pad, doc_y,
                              UI::Theme::Color{204, 204, 204, 255});
          }
        }
        doc_y += line_spacing;
      }
    }
  }

  // --- Signature Help Tooltip Overlay ---
  if (m_signature_help.is_visible() &&
      !m_signature_help.get_help().signatures.empty() && document != nullptr) {
    const std::string_view current_line =
        document->get_line(document->get_caret_line());
    const std::size_t caret_col = document->get_caret_column();
    const std::string_view prefix =
        current_line.substr(0, std::min(caret_col, current_line.size()));

    std::size_t open_count = 0;
    std::size_t close_count = 0;
    for (char ch : prefix) {
      if (ch == '(') ++open_count;
      else if (ch == ')') ++close_count;
    }

    if (open_count <= close_count) {
      m_signature_help.hide();
    } else {
      const auto &sig = m_signature_help.get_help().signatures[0];
      const float line_h = 20.0F * surface.m_dpi_scale;
      const float caret_screen_x =
          code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                       std::string{prefix}));
      const float line_top_y =
          layout.editor_bounds.y +
          static_cast<float>(physical_line_to_visual_row(
                                 m_folding, document->get_caret_line(),
                                 document->get_line_count()) -
                             m_scrollbar.get_first_visible_line()) *
              line_h;

      const int text_w = surface.m_editor_font->getTextWidth(sig.label);
      const float hint_w =
          static_cast<float>(text_w) + 16.0F * surface.m_dpi_scale;
      const float hint_h = 22.0F * surface.m_dpi_scale;
      const float hint_x = std::clamp(
          caret_screen_x, layout.editor_bounds.x + 10.0F,
          std::max(layout.editor_bounds.x + 10.0F,
                   layout.editor_bounds.right() - (hint_w + 20.0F)));

      const bool popup_visible =
          (m_completion_popup.is_visible() &&
           m_completion_popup.get_item_count() > 0);
      float hint_y = line_top_y - hint_h - 3.0F * surface.m_dpi_scale;

      if (hint_y < layout.editor_bounds.y + 2.0F) {
        if (popup_visible) {
          const std::size_t count = m_completion_popup.get_item_count();
          const std::size_t max_visible =
              m_completion_popup.get_max_visible_items();
          const std::size_t visible_count_pop =
              std::min<std::size_t>(count, max_visible);
          const float popup_h = static_cast<float>(visible_count_pop) * line_h +
                                4.0F * surface.m_dpi_scale;
          const float popup_bottom =
              line_top_y + line_h + popup_h + 4.0F * surface.m_dpi_scale;

          if (line_top_y >= layout.editor_bounds.y + 12.0F) {
            hint_y = std::max(layout.editor_bounds.y + 2.0F,
                              line_top_y - hint_h - 1.0F);
          } else if (popup_bottom + hint_h <
                     layout.editor_bounds.bottom() - 10.0F) {
            hint_y = popup_bottom;
          } else {
            hint_y = layout.editor_bounds.y + 2.0F;
          }
        } else {
          hint_y = line_top_y + line_h + 2.0F * surface.m_dpi_scale;
        }
      }

      const UI::Rect hint_bounds{hint_x, hint_y, hint_w, hint_h};

      const UI::Theme::Color hint_bg{24, 24, 30, 255};
      const UI::Theme::Color hint_border{55, 55, 68, 255};
      surface.fill_rounded_rectangle(
          drawable, hint_bounds, surface.allocate_color(hint_bg),
          3.0F * surface.m_dpi_scale, surface.m_pixels.editor_background);
      surface.draw_rectangle(drawable, hint_bounds,
                             surface.allocate_color(hint_border));

      surface.draw_text(drawable, *surface.m_editor_font, sig.label,
                        hint_bounds.x + 8.0F * surface.m_dpi_scale,
                        hint_bounds.y + hint_h * 0.5F,
                        UI::Theme::Color{78, 201, 176, 255});
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
  const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale -
                       m_text_scroll_offset;
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
