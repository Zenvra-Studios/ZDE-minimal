#include "Platform/Cocoa/Components/TextEditor.h"
#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Language/Protocol/LspProtocolSerializer.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

@interface ZDETabActionMenuHandler : NSObject
@property(nonatomic, assign)
    Zenvra::Platform::Cocoa::Components::TextEditor *editor;
- (void)onSplitUp:(id)sender;
- (void)onSplitDown:(id)sender;
- (void)onSplitLeft:(id)sender;
- (void)onSplitRight:(id)sender;
- (void)onCloseAll:(id)sender;
- (void)onCloseSaved:(id)sender;
@end

@implementation ZDETabActionMenuHandler
- (void)onSplitUp:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->split_editor();
}
- (void)onSplitDown:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->split_editor();
}
- (void)onSplitLeft:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->split_editor();
}
- (void)onSplitRight:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->split_editor();
}
- (void)onCloseAll:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->close_all_documents();
}
- (void)onCloseSaved:(id)sender {
  (void)sender;
  if (self.editor)
    self.editor->close_saved_documents();
}
@end

namespace Zenvra::Platform::Cocoa::Components {

namespace {

constexpr float FIXED_MINIMAP_WIDTH = 112.0F;
constexpr float FIXED_SCROLLBAR_WIDTH = 14.0F;
constexpr float MIN_PANE_WIDTH_FOR_MINIMAP = 120.0F;

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
      ++current_visual;
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
    if (!folding.is_line_hidden(i))
      ++visual_row;
  }
  return visual_row;
}

std::size_t
count_visible_lines(const UI::Components::EditorFoldingModel &folding,
                    std::size_t total_lines) {
  std::size_t visible = 0;
  for (std::size_t i = 0; i < total_lines; ++i) {
    if (!folding.is_line_hidden(i))
      ++visible;
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

  // Simplistic backward search for unmatched '{'
  int brace_depth = 0;
  bool found_open = false;
  const int search_limit = std::max(0, static_cast<int>(start_line) - 500);

  for (int line_idx = static_cast<int>(start_line); line_idx >= search_limit;
       --line_idx) {
    const std::string_view line =
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

  // Simplistic forward search for matching '}' starting from the open brace
  if (found_open) {
    brace_depth = 0;
    bool found_close = false;
    const std::size_t doc_lines = document.get_line_count();
    const std::size_t forward_limit =
        std::min(doc_lines, open_brace->line + 1500);

    for (std::size_t line_idx = open_brace->line; line_idx < forward_limit;
         ++line_idx) {
      const std::string_view line = document.get_line(line_idx);
      const std::size_t search_start =
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
  if (const auto *path_ptr = m_controller.get_active_path()) {
    return make_lsp_uri(path_ptr->string());
  }
  if (const auto *doc = m_controller.get_active_document()) {
    return make_lsp_uri(doc->get_file_name());
  }
  return "file:///untitled.cpp";
}

std::string TextEditor::get_active_document_filename() const {
  if (const auto *path_ptr = m_controller.get_active_path()) {
    return path_ptr->string();
  }
  if (const auto *doc = m_controller.get_active_document()) {
    return std::string(doc->get_file_name());
  }
  return "untitled.cpp";
}

void TextEditor::on_diagnostics_updated(
    const std::string &uri, std::vector<Language::Protocol::Diagnostic> diags) {
  std::lock_guard<std::mutex> lock(m_lsp_mutex);
  const std::string active_uri = get_active_document_uri();
  if (uri == active_uri || uri.ends_with(get_active_document_filename())) {
    if (auto *doc = m_controller.get_active_document()) {
      doc->set_diagnostics(std::move(diags));
    }
  }
}

void TextEditor::reset_split() noexcept {
  m_is_split = false;
  m_split_document_index.reset();
  m_focused_pane = SplitPaneFocus::Left;
  m_is_resizing_split = false;
  m_scrollbar.reset();
  m_split_scrollbar.reset();
}

void TextEditor::split_editor() noexcept {
  if (m_controller.get_active_document() != nullptr) {
    m_is_split = true;
    m_split_document_index = m_controller.get_active_index();
    m_focused_pane = SplitPaneFocus::Right;
  }
}

void TextEditor::close_all_documents() {
  static_cast<void>(m_controller.close_all_files());
  reset_split();
  m_hovered_tab_index.reset();
  m_hovered_tab_close_index.reset();
}

void TextEditor::close_saved_documents() {
  const auto docs = m_controller.get_documents();
  for (std::size_t i = docs.size(); i > 0; --i) {
    if (!docs[i - 1].text.is_dirty()) {
      static_cast<void>(m_controller.close_file(i - 1));
    }
  }
  m_scrollbar.reset();
  m_reveal_caret_pending = true;
  m_hovered_tab_index.reset();
  m_hovered_tab_close_index.reset();
}

bool TextEditor::switch_header_source() {
  const auto docs = m_controller.get_documents();
  auto cur_idx = m_controller.get_active_index();
  if (!cur_idx || *cur_idx >= docs.size())
    return false;

  const std::filesystem::path current_path = docs[*cur_idx].path;
  if (current_path.empty())
    return false;

  const std::string ext = current_path.extension().string();
  const std::string stem = current_path.stem().string();
  const std::filesystem::path parent = current_path.parent_path();

  std::vector<std::string> candidate_exts;
  if (ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".cxx" ||
      ext == ".mm" || ext == ".m") {
    candidate_exts = {".h", ".hpp", ".hxx", ".hh"};
  } else if (ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh") {
    candidate_exts = {".cpp", ".cc", ".c", ".cxx", ".mm", ".m"};
  } else {
    return false;
  }

  for (const auto &cand_ext : candidate_exts) {
    std::filesystem::path test_path = parent / (stem + cand_ext);
    if (std::filesystem::exists(test_path)) {
      return open_file(test_path);
    }
  }

  if (parent.filename() == "Source" || parent.filename() == "src") {
    for (const auto &cand_ext : candidate_exts) {
      std::filesystem::path test_path =
          parent.parent_path() / "Include" / (stem + cand_ext);
      if (std::filesystem::exists(test_path))
        return open_file(test_path);
      test_path = parent.parent_path() / "include" / (stem + cand_ext);
      if (std::filesystem::exists(test_path))
        return open_file(test_path);
    }
  } else if (parent.filename() == "Include" || parent.filename() == "include") {
    for (const auto &cand_ext : candidate_exts) {
      std::filesystem::path test_path =
          parent.parent_path() / "Source" / (stem + cand_ext);
      if (std::filesystem::exists(test_path))
        return open_file(test_path);
      test_path = parent.parent_path() / "src" / (stem + cand_ext);
      if (std::filesystem::exists(test_path))
        return open_file(test_path);
    }
  }

  return false;
}

bool TextEditor::format_document() {
  auto *doc = get_focused_document();
  if (!doc || doc->is_read_only())
    return false;

  std::vector<std::string> new_lines;
  new_lines.reserve(doc->get_line_count());
  for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
    std::string line = std::string(doc->get_line(i));
    while (!line.empty() &&
           (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
      line.pop_back();
    }
    new_lines.push_back(std::move(line));
  }
  doc->reload_contents(std::move(new_lines), "LF");
  return true;
}

bool TextEditor::go_to_definition() {
  const auto *doc = get_focused_document();
  if (!doc)
    return false;

  const std::string uri = get_active_document_uri();
  const std::string fname = get_active_document_filename();
  const std::size_t caret_line = doc->get_caret_line();
  const std::size_t caret_col = doc->get_caret_column();
  const Language::Protocol::Position pos{caret_line, caret_col};
  const std::string line_text = (caret_line < doc->get_line_count())
      ? std::string(doc->get_line(caret_line))
      : std::string();

  Language::LanguageServerManager::instance().request_definition(
      uri, fname, pos,
      [this](std::vector<Language::Protocol::Location> locations) {
        if (locations.empty())
          return;
        const auto &loc = locations[0];
        std::filesystem::path target_path =
            Language::Protocol::LspProtocolSerializer::uri_to_path(loc.uri);
        if (target_path.empty())
          return;
        std::error_code ec;
        if (std::filesystem::exists(target_path, ec)) {
          static_cast<void>(this->open_file(target_path));
          if (auto *target_doc = this->get_focused_document()) {
            target_doc->set_caret(loc.range.start.line,
                                  loc.range.start.character);
            this->m_reveal_caret_pending = true;
          }
        }
      },
      line_text);
  return true;
}

bool TextEditor::select_all_occurrences() {
  auto *doc = get_focused_document();
  if (!doc)
    return false;

  return doc->select_word_at(doc->get_caret_line(), doc->get_caret_column());
}

UI::Editor::TextDocumentModel *TextEditor::get_focused_document() noexcept {
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value()) {
    if (*m_split_document_index < m_controller.get_documents().size()) {
      return m_controller.get_document(*m_split_document_index);
    }
  }
  return m_controller.get_active_document();
}

const UI::Editor::TextDocumentModel *
TextEditor::get_focused_document() const noexcept {
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value()) {
    if (*m_split_document_index < m_controller.get_documents().size()) {
      return m_controller.get_document(*m_split_document_index);
    }
  }
  return m_controller.get_active_document();
}

bool TextEditor::is_split_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  if (!is_split_active() || !layout.editor_bounds.contains(point_x, point_y)) {
    return false;
  }
  const float split_x =
      layout.editor_bounds.x + layout.editor_bounds.width * m_split_ratio;
  const float grab_margin = 6.0F * layout.dpi_scale;
  return std::abs(point_x - split_x) <= grab_margin;
}

bool TextEditor::open_file(const std::filesystem::path &path) {
  const bool opened = m_controller.open_file(path);
  if (opened) {
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
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

      auto diags = Language::LanguageServerManager::instance()
                       .get_diagnostics_for_document(uri);
      if (!diags.empty()) {
        const_cast<UI::Editor::TextDocumentModel *>(doc)->set_diagnostics(
            std::move(diags));
      }
    }
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

      auto diags = Language::LanguageServerManager::instance()
                       .get_diagnostics_for_document(uri);
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

      auto diags = Language::LanguageServerManager::instance()
                       .get_diagnostics_for_document(uri);
      if (!diags.empty()) {
        const_cast<UI::Editor::TextDocumentModel *>(doc)->set_diagnostics(
            std::move(diags));
      }
    }
  }
  return created;
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py,
    bool extend, int clicks, std::string &cmd_out) {
  // Split close button click
  if (m_is_split && m_split_close_btn_bounds.contains(px, py)) {
    reset_split();
    return true;
  }

  // Split resize handle grab
  if (is_split_resize_handle_point(layout, px, py)) {
    m_is_resizing_split = true;
    return true;
  }

  // Tab Action Toolbar Buttons (0: Split, 1: Prev, 2: Next, 3: More)
  for (std::size_t i = 0; i < 4; ++i) {
    if (m_tab_action_bounds[i].contains(px, py)) {
      if (i == 0) {
        if (m_is_split) {
          reset_split();
        } else if (m_controller.get_active_document() != nullptr) {
          m_is_split = true;
          m_split_document_index = m_controller.get_active_index();
          m_focused_pane = SplitPaneFocus::Right;
        }
      } else if (i == 1) {
        if (auto cur = m_controller.get_active_index(); cur && *cur > 0) {
          static_cast<void>(m_controller.activate_file(*cur - 1));
        }
      } else if (i == 2) {
        if (auto cur = m_controller.get_active_index();
            cur && *cur + 1 < m_controller.get_documents().size()) {
          static_cast<void>(m_controller.activate_file(*cur + 1));
        }
      } else if (i == 3) {
        show_tab_action_menu(layout);
      }
      return true;
    }
  }

  // Tab bar interaction
  for (std::size_t i = 0; i < m_tab_count; ++i) {
    if (m_tab_bounds[i].contains(px, py)) {
      const float close_width =
          UI::Editor::StudioEditorMetrics::editor_tab_close_width *
          surface.m_dpi_scale;
      const UI::Rect close_bounds{m_tab_bounds[i].right() - close_width,
                                  m_tab_bounds[i].y, close_width,
                                  m_tab_bounds[i].height};
      m_focused = true;
      if (close_bounds.contains(px, py)) {
        const float shift = m_tab_bounds[i].width + UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
        const bool closed = m_controller.close_file(i);
        if (closed) {
          const auto remaining = m_controller.get_documents();
          for (std::size_t k = i; k < remaining.size(); ++k) {
            m_tab_animated_offset_x[remaining[k].id] += shift;
          }
          m_scrollbar.reset();
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
          m_hovered_tab_index.reset();
          m_hovered_tab_close_index.reset();
        }
        return closed;
      }
      if (m_controller.activate_file(i)) {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
      }
      m_tab_drag_drop.begin_drag(i, px);
      m_drag_initial_tab_x = m_tab_bounds[i].x;
      return true;
    }
  }

  UI::Editor::TextDocumentModel *document = get_focused_document();
  if (m_is_split) {
    const float split_x =
        layout.editor_bounds.x + layout.editor_bounds.width * m_split_ratio;
    m_focused_pane =
        (px >= split_x) ? SplitPaneFocus::Right : SplitPaneFocus::Left;
    document = get_focused_document();
  }

  const float line_height = 20.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));

  const float scale = surface.m_dpi_scale;
  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  if (is_split_active) {
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const float minimap_w = FIXED_MINIMAP_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, layout.editor_bounds.y,
                               splitter_x - layout.editor_bounds.x,
                               layout.editor_bounds.height};
    const float left_minimap_w =
        (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? minimap_w
                                                                  : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w,
                                  layout.editor_bounds.y, scrollbar_w,
                                  layout.editor_bounds.height};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w,
                                layout.editor_bounds.y, left_minimap_w,
                                layout.editor_bounds.height};

    const UI::Rect right_bounds{
        splitter_x + 2.0F * scale, layout.editor_bounds.y,
        layout.editor_bounds.right() - (splitter_x + 2.0F * scale),
        layout.editor_bounds.height};
    const float right_minimap_w =
        (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? minimap_w
                                                                   : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w,
                                   layout.editor_bounds.y, scrollbar_w,
                                   layout.editor_bounds.height};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w,
                                 layout.editor_bounds.y, right_minimap_w,
                                 layout.editor_bounds.height};

    auto *left_doc = m_controller.get_active_document();
    auto *right_doc = m_controller.get_document(*m_split_document_index);

    // Right Minimap
    if (right_doc != nullptr && !right_minimap.is_empty() &&
        right_minimap.contains(px, py)) {
      m_focused_pane = SplitPaneFocus::Right;
      UI::Editor::StudioEditorLayoutResult rlay = layout;
      rlay.minimap_bounds = right_minimap;
      rlay.scrollbar_bounds = right_scrollbar;
      m_split_scrollbar.synchronize(right_doc->get_line_count(), visible_count);
      if (const auto line = m_split_minimap.handle_pointer_press(
              rlay, px, py, right_doc->get_line_count(), visible_count,
              m_split_scrollbar.get_first_visible_line())) {
        static_cast<void>(m_split_scrollbar.scroll_to(*line));
      }
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return true;
    }

    // Right Scrollbar
    if (right_doc != nullptr && right_scrollbar.contains(px, py)) {
      m_focused_pane = SplitPaneFocus::Right;
      UI::Editor::StudioEditorLayoutResult rlay = layout;
      rlay.minimap_bounds = right_minimap;
      rlay.scrollbar_bounds = right_scrollbar;
      m_split_scrollbar.synchronize(right_doc->get_line_count(), visible_count);
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return m_split_scrollbar.handle_pointer_press(rlay, px, py);
    }

    // Left Minimap
    if (left_doc != nullptr && !left_minimap.is_empty() &&
        left_minimap.contains(px, py)) {
      m_focused_pane = SplitPaneFocus::Left;
      UI::Editor::StudioEditorLayoutResult llay = layout;
      llay.minimap_bounds = left_minimap;
      llay.scrollbar_bounds = left_scrollbar;
      m_scrollbar.synchronize(left_doc->get_line_count(), visible_count);
      if (const auto line = m_minimap.handle_pointer_press(
              llay, px, py, left_doc->get_line_count(), visible_count,
              m_scrollbar.get_first_visible_line())) {
        static_cast<void>(m_scrollbar.scroll_to(*line));
      }
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return true;
    }

    // Left Scrollbar
    if (left_doc != nullptr && left_scrollbar.contains(px, py)) {
      m_focused_pane = SplitPaneFocus::Left;
      UI::Editor::StudioEditorLayoutResult llay = layout;
      llay.minimap_bounds = left_minimap;
      llay.scrollbar_bounds = left_scrollbar;
      m_scrollbar.synchronize(left_doc->get_line_count(), visible_count);
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return m_scrollbar.handle_pointer_press(llay, px, py);
    }

    // Splitter Resize Handle
    if (is_split_resize_handle_point(layout, px, py)) {
      m_is_resizing_split = true;
      return true;
    }

    // Right Editor Pane (Code or Gutter)
    if (right_doc != nullptr &&
        (right_bounds.contains(px, py) || px >= splitter_x)) {
      m_focused_pane = SplitPaneFocus::Right;
      m_completion_popup.hide();
      m_signature_help.hide();
      m_focused = true;

      const float right_gutter_w = layout.gutter_bounds.width;
      const float fold_margin =
          UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
      const float fold_margin_left =
          splitter_x + 2.0F * scale + right_gutter_w - fold_margin;
      if (px >= fold_margin_left &&
          px <= splitter_x + 2.0F * scale + right_gutter_w) {
        const std::size_t split_total_lines = right_doc->get_line_count();
        const std::size_t clicked_row = static_cast<std::size_t>(std::max(
            static_cast<int>((py - layout.editor_bounds.y) / line_height), 0));
        const std::size_t split_line = visual_row_to_physical_line(
            m_split_folding,
            m_split_scrollbar.get_first_visible_line() + clicked_row,
            split_total_lines);

        if (m_split_folding.is_fold_start(split_line)) {
          m_split_folding.toggle_fold(split_line);
          m_split_scrollbar.synchronize(
              count_visible_lines(m_split_folding, split_total_lines),
              visible_count);
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
          return true;
        }
      }

      m_pointer_selecting = true;
      const UI::Editor::TextPosition pos =
          position_from_point(surface, layout, px, py);
      if (clicks >= 2) {
        right_doc->select_word_at(pos.line, pos.column);
      } else {
        static_cast<void>(right_doc->set_caret(pos.line, pos.column, extend));
      }
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
      const NSEventModifierFlags flags = [NSEvent modifierFlags];
      if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0) {
        static_cast<void>(go_to_definition());
      }
      return true;
    }

    // Left Editor Pane (Code or Gutter)
    if (left_doc != nullptr && (left_bounds.contains(px, py) ||
                                layout.gutter_bounds.contains(px, py))) {
      m_focused_pane = SplitPaneFocus::Left;
      m_completion_popup.hide();
      m_signature_help.hide();
      m_focused = true;

      const std::size_t total_lines = left_doc->get_line_count();
      m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                              visible_count);

      // Fold toggle
      if (const std::optional<std::size_t> fold_line = fold_start_line_at_point(
              m_folding, layout, px, py, surface.m_dpi_scale,
              m_scrollbar.get_first_visible_line(), total_lines)) {
        m_folding.toggle_fold(*fold_line);
        m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                                visible_count);
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        return true;
      }

      m_pointer_selecting = true;
      const UI::Editor::TextPosition position =
          position_from_point(surface, layout, px, py);
      if (clicks >= 2) {
        left_doc->select_word_at(position.line, position.column);
      } else {
        static_cast<void>(
            left_doc->set_caret(position.line, position.column, extend));
      }
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
      if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0) {
        static_cast<void>(go_to_definition());
      }
      return true;
    }
  }

  // Minimap (Single mode)
  if (document != nullptr && m_minimap.is_point(layout, px, py)) {
    m_scrollbar.synchronize(
        count_visible_lines(m_folding, document->get_line_count()),
        visible_count);
    if (const auto line = m_minimap.handle_pointer_press(
            layout, px, py, document->get_line_count(), visible_count,
            m_scrollbar.get_first_visible_line())) {
      static_cast<void>(m_scrollbar.scroll_to(*line));
    }
    m_focused = true;
    m_pointer_selecting = false;
    m_reveal_caret_pending = false;
    m_caret_blink.reset();
    return true;
  }
  // Scrollbar (Single mode)
  if (document != nullptr && m_scrollbar.is_point(layout, px, py)) {
    m_scrollbar.synchronize(
        count_visible_lines(m_folding, document->get_line_count()),
        visible_count);
    m_focused = true;
    m_pointer_selecting = false;
    m_reveal_caret_pending = false;
    m_caret_blink.reset();
    return m_scrollbar.handle_pointer_press(layout, px, py);
  }

  // Empty state buttons
  if (document == nullptr) {
    if (layout.editor_bounds.contains(px, py)) {
      if (m_empty_state_open_btn.handle_pointer_press(px, py)) {
        cmd_out = "zde.project.open";
        return true;
      }
      if (m_empty_state_clone_btn.handle_pointer_press(px, py)) {
        cmd_out = "zde.git.clone";
        return true;
      }
    }
  }

  if ((!layout.gutter_bounds.contains(px, py) &&
       !layout.editor_bounds.contains(px, py)) ||
      document == nullptr) {
    return false;
  }

  const std::size_t total_lines = document->get_line_count();
  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);

  // Fold toggle
  if (const std::optional<std::size_t> fold_line = fold_start_line_at_point(
          m_folding, layout, px, py, surface.m_dpi_scale,
          m_scrollbar.get_first_visible_line(), total_lines)) {
    m_folding.toggle_fold(*fold_line);
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                            visible_count);
    m_reveal_caret_pending = true;
    return true;
  }

  m_focused = true;
  m_pointer_selecting = true;
  const UI::Editor::TextPosition position =
      position_from_point(surface, layout, px, py);
  if (clicks >= 2) {
    document->select_word_at(position.line, position.column);
  } else {
    static_cast<void>(
        document->set_caret(position.line, position.column, extend));
  }
  m_reveal_caret_pending = true;
  m_caret_blink.reset();
  const NSEventModifierFlags flags = [NSEvent modifierFlags];
  if ((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0) {
    static_cast<void>(go_to_definition());
  }
  return true;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer &,
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) const noexcept {
  return layout.tab_bar_bounds.contains(px, py);
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) noexcept {
  std::optional<std::size_t> new_hovered;
  std::optional<std::size_t> new_close_hovered;
  const float close_width = 28.0F * layout.dpi_scale;
  for (std::size_t i = 0; i < m_tab_count; ++i) {
    if (m_tab_bounds[i].contains(px, py)) {
      new_hovered = i;
      if (px >= m_tab_bounds[i].right() - close_width) {
        new_close_hovered = i;
      }
      break;
    }
  }
  bool changed = new_hovered != m_hovered_tab_index;
  m_hovered_tab_index = new_hovered;
  changed |= new_close_hovered != m_hovered_tab_close_index;
  m_hovered_tab_close_index = new_close_hovered;

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float scale = layout.dpi_scale;
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_scrollbar{splitter_x - scrollbar_w,
                                  layout.editor_bounds.y, scrollbar_w,
                                  layout.editor_bounds.height};
    const UI::Rect right_scrollbar{layout.editor_bounds.right() - scrollbar_w,
                                   layout.editor_bounds.y, scrollbar_w,
                                   layout.editor_bounds.height};

    UI::Editor::StudioEditorLayoutResult llay = layout;
    llay.scrollbar_bounds = left_scrollbar;

    UI::Editor::StudioEditorLayoutResult rlay = layout;
    rlay.scrollbar_bounds = right_scrollbar;

    changed |= m_scrollbar.set_hovered(llay, px, py);
    changed |= m_split_scrollbar.set_hovered(rlay, px, py);
  } else {
    changed |= m_scrollbar.set_hovered(layout, px, py);
  }

  // Empty state button hover
  changed |= m_empty_state_open_btn.handle_pointer_move(px, py);
  changed |= m_empty_state_clone_btn.handle_pointer_move(px, py);

  // Tab Action Toolbar buttons (0..3)
  std::optional<std::size_t> next_act_hover;
  for (std::size_t i = 0; i < 4; ++i) {
    if (m_tab_action_bounds[i].contains(px, py)) {
      next_act_hover = i;
      break;
    }
  }
  if (next_act_hover != m_hovered_tab_action) {
    m_hovered_tab_action = next_act_hover;
    changed = true;
  }

  // Split close button hover
  const bool split_close_hover =
      m_is_split && m_split_close_btn_bounds.contains(px, py);
  if (split_close_hover != m_hovered_split_close) {
    m_hovered_split_close = split_close_hover;
    changed = true;
  }

  // Split resize handle hover
  const bool split_resize_hover = is_split_resize_handle_point(layout, px, py);
  if (split_resize_hover != m_hovered_split_resize) {
    m_hovered_split_resize = split_resize_hover;
    changed = true;
  }

  // Tab Action Menu hover
  if (m_tab_action_menu.visible) {
    std::optional<std::size_t> next_menu_hover;
    for (std::size_t i = 0; i < m_tab_action_menu.item_bounds.size(); ++i) {
      if (m_tab_action_menu.item_bounds[i].contains(px, py)) {
        next_menu_hover = i;
        break;
      }
    }
    if (next_menu_hover != m_tab_action_menu.hovered_index) {
      m_tab_action_menu.hovered_index = next_menu_hover;
      changed = true;
    }
  }

  // Fold margin hover
  std::optional<std::size_t> new_fold_line;
  if (const UI::Editor::TextDocumentModel *doc =
          m_controller.get_active_document()) {
    const float line_height = 20.0F;
    const std::size_t total_lines = doc->get_line_count();
    const float fold_margin =
        UI::Editor::StudioEditorMetrics::fold_margin_width;
    const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
    if (layout.gutter_bounds.contains(px, py) && px >= fold_margin_left) {
      const std::size_t clicked_row = static_cast<std::size_t>(std::max(
          static_cast<int>((py - layout.editor_bounds.y) / line_height), 0));
      const std::size_t line_index = visual_row_to_physical_line(
          m_folding, m_scrollbar.get_first_visible_line() + clicked_row,
          total_lines);
      if (m_folding.is_fold_start(line_index)) {
        new_fold_line = line_index;
      }
    }
  }
  changed |= new_fold_line != m_hovered_fold_line;
  m_hovered_fold_line = new_fold_line;
  return changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py) {
  if (m_is_resizing_split) {
    m_split_ratio =
        std::clamp((px - layout.editor_bounds.x) / layout.editor_bounds.width,
                   0.15F, 0.85F);
    return true;
  }
  if (m_tab_drag_drop.is_dragging()) {
    m_tab_drag_drop.drag(px);
    return true;
  }

  const float scale = surface.m_dpi_scale;
  const float line_height = 20.0F * scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  if (is_split_active) {
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const float minimap_w = FIXED_MINIMAP_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, layout.editor_bounds.y,
                               splitter_x - layout.editor_bounds.x,
                               layout.editor_bounds.height};
    const float left_minimap_w =
        (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? minimap_w
                                                                  : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w,
                                  layout.editor_bounds.y, scrollbar_w,
                                  layout.editor_bounds.height};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w,
                                layout.editor_bounds.y, left_minimap_w,
                                layout.editor_bounds.height};

    const UI::Rect right_bounds{
        splitter_x + 2.0F * scale, layout.editor_bounds.y,
        layout.editor_bounds.right() - (splitter_x + 2.0F * scale),
        layout.editor_bounds.height};
    const float right_minimap_w =
        (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? minimap_w
                                                                   : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w,
                                   layout.editor_bounds.y, scrollbar_w,
                                   layout.editor_bounds.height};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w,
                                 layout.editor_bounds.y, right_minimap_w,
                                 layout.editor_bounds.height};

    UI::Editor::StudioEditorLayoutResult llay = layout;
    llay.minimap_bounds = left_minimap;
    llay.scrollbar_bounds = left_scrollbar;

    UI::Editor::StudioEditorLayoutResult rlay = layout;
    rlay.minimap_bounds = right_minimap;
    rlay.scrollbar_bounds = right_scrollbar;

    if (auto *left_doc = m_controller.get_active_document()) {
      if (const auto target = m_minimap.handle_pointer_drag(
              llay, py, left_doc->get_line_count(), visible_count,
              m_scrollbar.get_first_visible_line())) {
        static_cast<void>(m_scrollbar.scroll_to(*target));
        m_reveal_caret_pending = false;
        return true;
      }
      if (m_scrollbar.handle_pointer_drag(llay, py)) {
        m_reveal_caret_pending = false;
        return true;
      }
    }

    if (auto *right_doc = m_controller.get_document(*m_split_document_index)) {
      if (const auto target = m_split_minimap.handle_pointer_drag(
              rlay, py, right_doc->get_line_count(), visible_count,
              m_split_scrollbar.get_first_visible_line())) {
        static_cast<void>(m_split_scrollbar.scroll_to(*target));
        m_reveal_caret_pending = false;
        return true;
      }
      if (m_split_scrollbar.handle_pointer_drag(rlay, py)) {
        m_reveal_caret_pending = false;
        return true;
      }
    }

    if (m_pointer_selecting) {
      if (m_focused_pane == SplitPaneFocus::Right) {
        if (auto *right_doc =
                m_controller.get_document(*m_split_document_index)) {
          const auto pos = position_from_point(surface, layout, px, py);
          const bool chg = right_doc->set_caret(pos.line, pos.column, true);
          if (chg) {
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
          }
          return chg;
        }
      } else {
        if (auto *left_doc = m_controller.get_active_document()) {
          const auto pos = position_from_point(surface, layout, px, py);
          const bool chg = left_doc->set_caret(pos.line, pos.column, true);
          if (chg) {
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
          }
          return chg;
        }
      }
    }
    return false;
  }

  if (m_scrollbar.handle_pointer_drag(layout, py))
    return true;
  if (const UI::Editor::TextDocumentModel *doc = get_focused_document()) {
    m_scrollbar.synchronize(
        count_visible_lines(m_folding, doc->get_line_count()), visible_count);
    if (const auto line = m_minimap.handle_pointer_drag(
            layout, py, doc->get_line_count(), visible_count,
            m_scrollbar.get_first_visible_line())) {
      static_cast<void>(m_scrollbar.scroll_to(*line));
      return true;
    }
  }
  if (m_pointer_selecting) {
    auto pos = position_from_point(surface, layout, px, py);
    if (auto *doc = get_focused_document()) {
      doc->set_caret(pos.line, pos.column, true);
    }
    return true;
  }
  return false;
}

bool TextEditor::handle_pointer_release() noexcept {
  m_pointer_selecting = false;
  bool r = false;
  if (m_is_resizing_split) {
    m_is_resizing_split = false;
    r = true;
  }
  if (m_tab_drag_drop.is_dragging()) {
    m_tab_drag_drop.end_drag();
    return true;
  }
  r |= m_scrollbar.handle_pointer_release();
  r |= m_split_scrollbar.handle_pointer_release();
  r |= m_minimap.handle_pointer_release();
  r |= m_split_minimap.handle_pointer_release();
  return r;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float px, float py,
    std::string &cmd_out, std::ptrdiff_t delta, bool horiz) noexcept {
  (void)cmd_out;
  if (layout.tab_bar_bounds.contains(px, py)) {
    const float speed = 32.0F * layout.dpi_scale;
    m_tab_scroll_offset += static_cast<float>(delta) * speed;
    m_tab_scroll_offset =
        std::clamp(m_tab_scroll_offset, 0.0f, m_max_tab_scroll);
    return true;
  }
  if (horiz) {
    if (!m_is_split && layout.editor_bounds.contains(px, py)) {
      const float speed = 32.0F * layout.dpi_scale;
      m_text_scroll_offset += static_cast<float>(delta) * speed;
      m_text_scroll_offset =
          std::clamp(m_text_scroll_offset, 0.0f, m_max_text_scroll);
      return true;
    }
    return false;
  }

  const float scale = surface.m_dpi_scale;
  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  if (is_split_active && px >= splitter_x) {
    if (const UI::Editor::TextDocumentModel *split_doc =
            m_controller.get_document(*m_split_document_index)) {
      const float line_height = 20.0F * scale;
      const std::size_t visible_count = static_cast<std::size_t>(std::max(
          static_cast<int>(layout.editor_bounds.height / line_height), 1));
      m_split_scrollbar.synchronize(split_doc->get_line_count(), visible_count);
      return m_split_scrollbar.scroll_lines(delta);
    }
  } else {
    if (const UI::Editor::TextDocumentModel *document =
            m_controller.get_active_document()) {
      const float line_height = 20.0F * scale;
      const std::size_t visible_count = static_cast<std::size_t>(std::max(
          static_cast<int>(layout.editor_bounds.height / line_height), 1));
      m_scrollbar.synchronize(document->get_line_count(), visible_count);
    }
    m_reveal_caret_pending = false;
    return m_scrollbar.scroll_lines(delta);
  }
  return false;
}

bool TextEditor::handle_input(UI::Editor::EditorInputCommand cmd, bool extend) {
  {
    std::lock_guard<std::mutex> lock(m_lsp_mutex);
    if (m_completion_popup.is_visible()) {
      if (cmd == UI::Editor::EditorInputCommand::MoveUp) {
        m_completion_popup.select_previous();
        return true;
      }
      if (cmd == UI::Editor::EditorInputCommand::MoveDown) {
        m_completion_popup.select_next();
        return true;
      }
      if (cmd == UI::Editor::EditorInputCommand::Escape ||
          cmd == UI::Editor::EditorInputCommand::MoveLeft ||
          cmd == UI::Editor::EditorInputCommand::MoveRight ||
          cmd == UI::Editor::EditorInputCommand::MoveHome ||
          cmd == UI::Editor::EditorInputCommand::MoveEnd) {
        m_completion_popup.hide();
        m_signature_help.hide();
        if (cmd == UI::Editor::EditorInputCommand::Escape) {
          return true;
        }
      }
      if (cmd == UI::Editor::EditorInputCommand::InsertTab ||
          cmd == UI::Editor::EditorInputCommand::InsertNewLine) {
        if (const auto *item = m_completion_popup.get_selected_item()) {
          if (auto *doc = m_controller.get_active_document(); doc != nullptr) {
            const std::string_view current_line =
                doc->get_line(doc->get_caret_line());
            const std::size_t caret_col = doc->get_caret_column();

            // Find how many characters of the current token to replace before
            // caret
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

            const std::size_t prefix_len = caret_col - word_start;
            for (std::size_t k = 0; k < prefix_len; ++k) {
              static_cast<void>(m_controller.execute_input(
                  UI::Editor::EditorInputCommand::DeleteBackward));
            }

            std::string text_to_insert =
                item->insert_text.empty() ? item->label : item->insert_text;
            std::string clean_text;
            for (std::size_t idx = 0; idx < text_to_insert.size(); ++idx) {
              if (text_to_insert[idx] == '$' &&
                  idx + 1 < text_to_insert.size() &&
                  (text_to_insert[idx + 1] == '0' ||
                   text_to_insert[idx + 1] == '1')) {
                ++idx;
                continue;
              }
              clean_text += text_to_insert[idx];
            }

            static_cast<void>(m_controller.insert_text(clean_text));
            m_completion_popup.hide();
            return true;
          }
        }
      }
    }
  }

  const bool changed = m_controller.execute_input(cmd, extend);
  if (changed) {
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

      // Auto-hide signature help on newline or escape
      if (cmd == UI::Editor::EditorInputCommand::InsertNewLine ||
          cmd == UI::Editor::EditorInputCommand::Escape) {
        m_signature_help.hide();
      }

      // If Backspace or Delete occurred while completion popup was open,
      // auto-close or re-filter
      if (cmd == UI::Editor::EditorInputCommand::DeleteBackward ||
          cmd == UI::Editor::EditorInputCommand::DeleteForward) {
        if (m_completion_popup.is_visible()) {
          const std::string_view current_line =
              doc->get_line(doc->get_caret_line());
          const std::size_t caret_col = doc->get_caret_column();
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
          const std::string_view current_word =
              current_line.substr(word_start, caret_col - word_start);
          if (current_word.empty()) {
            m_completion_popup.hide();
          } else {
            m_completion_popup.set_filter(current_word);
            if (m_completion_popup.get_item_count() == 0) {
              m_completion_popup.hide();
            }
          }
        }
      }
    }
  }
  return changed;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action) {
  const bool res = m_controller.execute_action(action);
  if (res && action == UI::Editor::EditorAction::SaveDocument) {
    if (auto *doc = m_controller.get_active_document(); doc != nullptr) {
      const std::string uri = get_active_document_uri();
      const std::string fname = get_active_document_filename();
      Language::LanguageServerManager::instance().on_document_saved(uri, fname);
    }
  }
  return res;
}

std::optional<bool> TextEditor::handle_command(std::string_view id) {
  if (id == Commands::CommandIds::view_split_right ||
      id == Commands::CommandIds::view_split_down ||
      id == Commands::CommandIds::view_split_left ||
      id == Commands::CommandIds::view_split_up) {
    if (!m_is_split && m_controller.get_active_document() != nullptr) {
      m_is_split = true;
      m_split_document_index = m_controller.get_active_index();
      m_focused_pane = SplitPaneFocus::Right;
      return true;
    } else if (m_is_split) {
      m_focused_pane = (m_focused_pane == SplitPaneFocus::Left)
                           ? SplitPaneFocus::Right
                           : SplitPaneFocus::Left;
      return true;
    }
    return false;
  }

  if (id == "zde.editor.goToDefinition" || id == "zde.editor.goToDeclaration" ||
      id == "zde.editor.goToImplementation") {
    return go_to_definition();
  }
  if (id == "zde.editor.switchHeaderSource") {
    return switch_header_source();
  }
  if (id == "zde.editor.formatDocument" ||
      id == "zde.editor.formatDocumentWith") {
    return format_document();
  }
  if (id == "zde.selection.selectAllOccurrences") {
    return select_all_occurrences();
  }

  auto action = UI::Editor::EditorController::action_from_command_id(id);
  if (!action)
    return std::nullopt;
  return handle_action(*action);
}

std::optional<bool>
TextEditor::is_command_enabled(std::string_view id) const noexcept {
  if (id == Commands::CommandIds::view_split_right ||
      id == Commands::CommandIds::view_split_down ||
      id == Commands::CommandIds::view_split_left ||
      id == Commands::CommandIds::view_split_up) {
    return m_controller.get_active_document() != nullptr;
  }
  auto action = UI::Editor::EditorController::action_from_command_id(id);
  if (!action)
    return std::nullopt;
  return m_controller.can_execute_action(*action);
}

bool TextEditor::handle_text_input(std::string_view utf8) {
  const bool changed = m_controller.insert_text(utf8);
  if (changed) {
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

      const std::string_view current_line =
          doc->get_line(doc->get_caret_line());
      const std::size_t caret_col = doc->get_caret_column();

      // Extract the active word token before cursor
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
      const std::string_view current_word =
          current_line.substr(word_start, caret_col - word_start);

      const bool is_include_context =
          current_line.find('#') != std::string_view::npos || utf8 == "<" ||
          utf8 == "\"" || utf8 == "#";
      const bool is_trigger_char = utf8 == "." || utf8 == ">" || utf8 == ":" ||
                                   utf8 == "/" || utf8 == "\\" || utf8 == "(" ||
                                   utf8 == "," || is_include_context ||
                                   current_word.size() >= 1;

      if (utf8 == "(") {
        Language::Protocol::Position sig_pos{.line = doc->get_caret_line(),
                                             .character =
                                                 doc->get_caret_column()};
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
      } else {
        std::lock_guard<std::mutex> lock(m_lsp_mutex);
        m_signature_help.hide();
      }

      if (utf8 == " " || utf8 == ";" || utf8 == ")" || utf8 == "}") {
        std::lock_guard<std::mutex> lock(m_lsp_mutex);
        m_completion_popup.hide();
      } else if (is_trigger_char || m_completion_popup.is_visible()) {
        std::lock_guard<std::mutex> lock(m_lsp_mutex);
        const std::string_view full_line = doc->get_line(doc->get_caret_line());
        const std::size_t col = doc->get_caret_column();
        const std::string_view line_prefix =
            full_line.substr(0, std::min(col, full_line.size()));

        bool is_include_context = false;
        std::string header_query;

        std::size_t inc_pos = line_prefix.find("#include");
        if (inc_pos == std::string_view::npos)
          inc_pos = line_prefix.find("#import");

        if (inc_pos != std::string_view::npos) {
          const std::string_view after_inc = line_prefix.substr(inc_pos);
          auto lt = after_inc.rfind('<');
          auto qt = after_inc.rfind('"');
          if (lt != std::string_view::npos) {
            is_include_context = true;
            header_query = std::string(after_inc.substr(lt + 1));
          } else if (qt != std::string_view::npos) {
            is_include_context = true;
            header_query = std::string(after_inc.substr(qt + 1));
          }
        }

        const std::string active_query =
            is_include_context ? header_query : std::string(current_word);

        // Synchronously update filter immediately with zero latency
        if (m_completion_popup.is_visible()) {
          m_completion_popup.set_filter(active_query);
        } else {
          std::vector<Language::Protocol::CompletionItem> local_items;

          if (is_include_context) {
            // In #include / #import context, prioritize header libraries &
            // workspace files
            local_items =
                Language::LanguageServerManager::get_header_completions(
                    line_prefix, std::filesystem::current_path());
          } else {
            // Instant Local Suggestions (Language Templates & Buffer
            // Identifiers)
            auto templates =
                Language::LanguageServerManager::get_templates_for_filename(
                    fname);
            for (auto &tpl : templates) {
              local_items.push_back(std::move(tpl));
            }

            // Extract tokens from current document
            std::unordered_set<std::string> doc_words;
            for (std::size_t li = 0; li < doc->get_line_count(); ++li) {
              const std::string_view l = doc->get_line(li);
              std::size_t w_start = 0;
              for (std::size_t ci = 0; ci <= l.size(); ++ci) {
                const bool is_wc =
                    (ci < l.size() &&
                     (std::isalnum(static_cast<unsigned char>(l[ci])) ||
                      l[ci] == '_'));
                if (!is_wc) {
                  if (ci > w_start + 2) {
                    const std::string word(l.substr(w_start, ci - w_start));
                    if (word != current_word && doc_words.insert(word).second) {
                      Language::Protocol::CompletionItem item{};
                      item.label = word;
                      item.kind =
                          Language::Protocol::CompletionItemKind::Variable;
                      local_items.push_back(std::move(item));
                    }
                  }
                  w_start = ci + 1;
                }
              }
            }
          }

          if (!local_items.empty()) {
            m_completion_popup.show(std::move(local_items), 0.0F, 0.0F);
            m_completion_popup.set_filter(active_query);
          }
        }

        // Asynchronously query LSP for rich compiler-level completions
        Language::Protocol::Position pos{.line = doc->get_caret_line(),
                                         .character = doc->get_caret_column()};
        Language::LanguageServerManager::instance().request_completion(
            uri, fname, pos, doc->get_line(doc->get_caret_line()),
            [this](std::vector<Language::Protocol::CompletionItem> items) {
              std::lock_guard<std::mutex> lk(m_lsp_mutex);
              if (!items.empty()) {
                if (m_completion_popup.is_visible()) {
                  m_completion_popup.merge_items(std::move(items));
                } else {
                  m_completion_popup.show(std::move(items), 0.0F, 0.0F);
                }

                if (const auto *current_doc =
                        m_controller.get_active_document()) {
                  const std::string_view line =
                      current_doc->get_line(current_doc->get_caret_line());
                  const std::size_t col = current_doc->get_caret_column();
                  std::size_t start = std::min(col, line.size());
                  while (start > 0) {
                    const char ch = line[start - 1];
                    if (std::isalnum(static_cast<unsigned char>(ch)) ||
                        ch == '_' || ch == '#' || ch == '~') {
                      --start;
                    } else {
                      break;
                    }
                  }
                  const std::string_view latest_word =
                      line.substr(start, col - start);
                  m_completion_popup.set_filter(latest_word);
                }

                if (m_completion_popup.get_item_count() == 0) {
                  m_completion_popup.hide();
                }
              }
            });
      }
    }
  }
  return changed;
}
bool TextEditor::is_focused() const noexcept { return m_focused; }
void TextEditor::set_focused(bool focused) noexcept { m_focused = focused; }
bool TextEditor::is_empty_state_interactive_point(float px,
                                                  float py) const noexcept {
  return m_empty_state_open_btn.get_bounds().contains(px, py) ||
         m_empty_state_clone_btn.get_bounds().contains(px, py);
}
bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult &l, float px,
    float py) const noexcept {
  const float scale = l.dpi_scale;
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        l.editor_bounds.x +
        (l.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_scrollbar{splitter_x - scrollbar_w, l.editor_bounds.y,
                                  scrollbar_w, l.editor_bounds.height};
    const UI::Rect right_scrollbar{l.editor_bounds.right() - scrollbar_w,
                                   l.editor_bounds.y, scrollbar_w,
                                   l.editor_bounds.height};
    return left_scrollbar.contains(px, py) || right_scrollbar.contains(px, py);
  }
  return m_scrollbar.is_point(l, px, py);
}

bool TextEditor::is_minimap_point(const UI::Editor::StudioEditorLayoutResult &l,
                                  float px, float py) const noexcept {
  const float scale = l.dpi_scale;
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        l.editor_bounds.x +
        (l.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const float minimap_w = FIXED_MINIMAP_WIDTH * scale;
    const float left_w = splitter_x - l.editor_bounds.x;
    const float right_w = l.editor_bounds.right() -
                          (splitter_x + 2.0F * scale + l.gutter_bounds.width);
    const bool has_left_minimap = left_w >= MIN_PANE_WIDTH_FOR_MINIMAP * scale;
    const bool has_right_minimap =
        right_w >= MIN_PANE_WIDTH_FOR_MINIMAP * scale;
    const UI::Rect left_minimap{splitter_x - scrollbar_w - minimap_w,
                                l.editor_bounds.y, minimap_w,
                                l.editor_bounds.height};
    const UI::Rect right_minimap{
        l.editor_bounds.right() - scrollbar_w - minimap_w, l.editor_bounds.y,
        minimap_w, l.editor_bounds.height};
    return (has_left_minimap && left_minimap.contains(px, py)) ||
           (has_right_minimap && right_minimap.contains(px, py));
  }
  return m_minimap.is_point(l, px, py);
}
bool TextEditor::is_fold_margin_point(
    const StudioWorkspaceRenderer &,
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) const noexcept {
  const float scale = layout.dpi_scale;
  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float right_gutter_w = layout.gutter_bounds.width;
    const UI::Rect left_gutter = layout.gutter_bounds;
    const UI::Rect right_gutter{splitter_x + 2.0F * scale,
                                layout.editor_bounds.y, right_gutter_w,
                                layout.editor_bounds.height};
    const bool in_left_gutter = left_gutter.contains(px, py) &&
                                px >= (left_gutter.right() - fold_margin);
    const bool in_right_gutter = right_gutter.contains(px, py) &&
                                 px >= (right_gutter.right() - fold_margin);
    return in_left_gutter || in_right_gutter;
  }
  const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
  return layout.gutter_bounds.contains(px, py) && px >= fold_margin_left;
}

bool TextEditor::tick_animations() noexcept {
  bool needs_redraw = m_focused &&
                      (m_controller.get_active_document() != nullptr ||
                       (m_is_split && m_split_document_index.has_value())) &&
                      m_caret_blink.tick();

  const auto reloaded = m_controller.reload_externally_modified_files();
  if (!reloaded.empty()) {
    needs_redraw = true;
  }

  // Lerp animated tab sliding offsets (smooth sliding when tabs are closed)
  bool animating = false;
  for (auto &[id, offset_x] : m_tab_animated_offset_x) {
    if (std::abs(offset_x) > 0.5F) {
      offset_x += (0.0F - offset_x) * 0.3F;
      animating = true;
    } else {
      offset_x = 0.0F;
    }
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
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  draw_tab_strip(surface, context, layout);
  if (!layout.editor_header_bounds.is_empty() &&
      layout.editor_header_bounds.height > 2.0F) {
    draw_editor_header(surface, context, layout);
  }
  draw_document(surface, context, layout);
  draw_tab_action_menu(surface, context, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const std::span<const UI::Editor::EditorSessionDocument> documents =
      m_controller.get_documents();

  float total_width = 0.0F;
  for (std::size_t index = 0; index < documents.size(); ++index) {
    total_width += UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{documents[index].text.get_file_name()})),
        surface.m_dpi_scale);
    total_width +=
        UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
  }

  m_max_tab_scroll = std::max(0.0F, total_width - layout.tab_bar_bounds.width);
  if (m_max_tab_scroll == 0.0F) {
    const_cast<TextEditor *>(this)->m_tab_scroll_offset = 0.0F;
  }

  m_tab_count = 0;
  float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
  const float right_limit = layout.tab_bar_bounds.right();
  const std::optional<std::size_t> active_index =
      m_controller.get_active_index();

  for (std::size_t index = 0; index < documents.size(); ++index) {
    const UI::Editor::TextDocumentModel &document = documents[index].text;
    const float width = UI::Editor::calculate_editor_tab_width(
        static_cast<float>(surface.m_ui_font->getTextWidth(
            std::string{document.get_file_name()})),
        surface.m_dpi_scale);
    if (tab_x > right_limit) {
      break;
    }
    UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                    layout.tab_bar_bounds.height};

    const std::size_t id = documents[index].id;
    const float offset_x = m_tab_animated_offset_x.contains(id) ? m_tab_animated_offset_x[id] : 0.0F;

    if (m_tab_drag_drop.is_dragging() &&
        m_tab_drag_drop.get_dragged_index() == index) {
      bounds.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
    } else {
      bounds.x = tab_x + offset_x;
    }

    if (m_tab_count < max_visible_tabs) {
      m_tab_bounds[m_tab_count] = bounds;
      ++m_tab_count;
    }
    tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap *
                         surface.m_dpi_scale;
  }

  if (m_tab_animated_offset_x.size() > documents.size() + 8) {
    std::unordered_set<std::size_t> active_ids;
    for (const auto &doc : documents) {
      active_ids.insert(doc.id);
    }
    std::erase_if(m_tab_animated_offset_x, [&](const auto &item) { return !active_ids.contains(item.first); });
  }

  auto draw_single_tab = [&](std::size_t tab_index) {
    const std::size_t index = tab_index;
    const UI::Editor::TextDocumentModel &document = documents[index].text;
    const bool active = active_index && *active_index == index;
    const UI::Rect &bounds = m_tab_bounds[tab_index];
    const bool close_hovered =
        m_hovered_tab_close_index && *m_hovered_tab_close_index == tab_index;
    const bool tab_hovered =
        m_hovered_tab_index && *m_hovered_tab_index == tab_index;

    surface.fill_rectangle(context, bounds,
                           active ? surface.m_colors.tab_active_background
                           : tab_hovered
                               ? surface.m_colors.active_line_background
                               : surface.m_colors.tab_background);

    const int tab_left = round_to_int(bounds.x);
    const int tab_right = round_to_int(bounds.right()) - 1;
    const int tab_top = round_to_int(bounds.y);
    const int tab_bottom = round_to_int(bounds.bottom()) - 1;

    surface.draw_line(context, tab_left, tab_top, tab_right, tab_top,
                      surface.m_colors.border);
    surface.draw_line(context, tab_left, tab_top, tab_left, tab_bottom,
                      surface.m_colors.border);
    surface.draw_line(context, tab_right, tab_top, tab_right, tab_bottom,
                      surface.m_colors.border);

    const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
        std::filesystem::path{std::string{document.get_file_name()}});
    surface.draw_svg_icon(
        context, "Assets/icons/" + icon_asset,
        round_to_int(
            bounds.x +
            (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
                surface.m_dpi_scale),
        round_to_int(bounds.y + bounds.height * 0.5F),
        std::max(round_to_int(14.0F * surface.m_dpi_scale), 10),
        active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
        active ? surface.m_palette.tab_active_background
               : surface.m_palette.tab_background,
        true);

    const float text_x =
        bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                       surface.m_dpi_scale;
    const float center_y = bounds.y + bounds.height * 0.5F;
    surface.draw_text(context, *surface.m_ui_font, document.get_file_name(),
                      text_x, center_y,
                      active ? surface.m_text.primary : surface.m_text.muted,
                      &layout.tab_bar_bounds);

    const float close_cx =
        bounds.right() -
        UI::Editor::StudioEditorMetrics::editor_tab_close_width * 0.5F *
            surface.m_dpi_scale;
    if (close_hovered) {
      surface.draw_svg_icon(
          context, "Assets/icons/close-minimal.svg", round_to_int(close_cx),
          round_to_int(center_y),
          std::max(round_to_int(11.0F * surface.m_dpi_scale), 9),
          active ? surface.m_palette.text_primary
                 : surface.m_palette.text_muted,
          active ? surface.m_palette.tab_active_background
                 : surface.m_palette.tab_background);
    } else if (document.is_dirty()) {
      surface.draw_svg_icon(
          context, "Assets/icons/dirty.svg", round_to_int(close_cx),
          round_to_int(center_y),
          std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
          surface.m_palette.warning,
          active ? surface.m_palette.tab_active_background
                 : surface.m_palette.tab_background);
    }
  };

  surface.push_clip(context, layout.tab_bar_bounds);
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
  surface.pop_clip(context);

  const int tab_bar_bottom = round_to_int(layout.tab_bar_bounds.bottom()) - 1;
  const int tab_bar_left = round_to_int(layout.tab_bar_bounds.x);
  const int tab_bar_right = round_to_int(layout.tab_bar_bounds.right());

  if (active_index && *active_index < m_tab_count) {
    const UI::Rect &active_bounds = m_tab_bounds[*active_index];
    const int active_left = round_to_int(active_bounds.x);
    const int active_right = round_to_int(active_bounds.right()) - 1;

    surface.draw_line(context, tab_bar_left, tab_bar_bottom, active_left,
                      tab_bar_bottom, surface.m_colors.border);
    surface.draw_line(context, active_right, tab_bar_bottom, tab_bar_right,
                      tab_bar_bottom, surface.m_colors.border);
  } else {
    surface.draw_line(context, tab_bar_left, tab_bar_bottom, tab_bar_right,
                      tab_bar_bottom, surface.m_colors.border);
  }

  if (m_max_tab_scroll > 0.0F) {
    const float track_width = layout.tab_bar_bounds.width;
    const float thumb_width = std::max(
        20.0F * surface.m_dpi_scale,
        track_width * (track_width / (track_width + m_max_tab_scroll)));
    const float thumb_x =
        layout.tab_bar_bounds.x +
        (m_tab_scroll_offset / m_max_tab_scroll) * (track_width - thumb_width);
    const UI::Rect thumb_bounds{
        thumb_x, layout.tab_bar_bounds.bottom() - 3.0F * surface.m_dpi_scale,
        thumb_width, 3.0F * surface.m_dpi_scale};

    CGFloat thumb_rgba[4];
    StudioWorkspaceRenderer::color_to_rgba(m_hovered_tab_scrollbar
                                               ? surface.m_palette.text_primary
                                               : surface.m_palette.text_muted,
                                           thumb_rgba);
    surface.fill_rectangle(context, thumb_bounds, thumb_rgba);
  }
}

void TextEditor::draw_editor_header(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const float scale = surface.m_dpi_scale;
  const UI::Rect header_bounds = layout.editor_header_bounds;
  const auto *document = m_controller.get_active_document();

  surface.fill_rectangle(context, header_bounds,
                         surface.m_colors.editor_background);

  const int header_bottom = round_to_int(header_bounds.bottom()) - 1;
  surface.draw_line(context, round_to_int(header_bounds.x), header_bottom,
                    round_to_int(header_bounds.right()), header_bottom,
                    surface.m_colors.border);

  const float btn_w = 26.0F * scale;
  const float btn_h = 22.0F * scale;
  const float btn_y = header_bounds.y + (header_bounds.height - btn_h) * 0.5F;
  const float center_y = header_bounds.y + header_bounds.height * 0.5F;

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const UI::Rect left_header{header_bounds.x, header_bounds.y,
                               splitter_x - header_bounds.x,
                               header_bounds.height};
    const UI::Rect right_header{splitter_x, header_bounds.y,
                                header_bounds.right() - splitter_x,
                                header_bounds.height};

    // Left side: File title with Icon
    if (document != nullptr) {
      const std::string filename{document->get_file_name()};
      const std::string icon_asset =
          UI::Editor::file_icon_asset_for_path(std::filesystem::path{filename});
      const int icon_sz = std::max(round_to_int(13.0F * scale), 11);
      const float icon_cx = left_header.x + 12.0F * scale + icon_sz * 0.5F;
      surface.draw_svg_icon(context, "Assets/icons/" + icon_asset,
                            round_to_int(icon_cx), round_to_int(center_y),
                            icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background, true);

      if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, filename,
                          left_header.x + 12.0F * scale + icon_sz +
                              6.0F * scale,
                          center_y, surface.m_text.primary);
      }
    }

    // Right side: Close button
    m_split_close_btn_bounds = UI::Rect{
        right_header.right() - btn_w - 4.0F * scale, btn_y, btn_w, btn_h};
    if (m_hovered_split_close) {
      surface.fill_rounded_rectangle(context, m_split_close_btn_bounds,
                                     surface.m_colors.hover_background,
                                     3.0F * scale);
    }
    surface.draw_svg_icon(
        context, "Assets/icons/close-minimal.svg",
        round_to_int(m_split_close_btn_bounds.x + btn_w * 0.5F),
        round_to_int(center_y), std::max(round_to_int(12.0F * scale), 10),
        m_hovered_split_close ? surface.m_palette.text_primary
                              : surface.m_palette.text_muted,
        surface.m_palette.editor_background, false);

    const float actions_right = m_split_close_btn_bounds.x - 2.0F * scale;
    m_tab_action_bounds[3] =
        UI::Rect{actions_right - 1.0F * btn_w, btn_y, btn_w, btn_h};
    m_tab_action_bounds[2] = UI::Rect{
        actions_right - 2.0F * btn_w - 2.0F * scale, btn_y, btn_w, btn_h};
    m_tab_action_bounds[1] = UI::Rect{
        actions_right - 3.0F * btn_w - 4.0F * scale, btn_y, btn_w, btn_h};
    m_tab_action_bounds[0] = UI::Rect{
        actions_right - 4.0F * btn_w - 6.0F * scale, btn_y, btn_w, btn_h};

    // Right Header File Title with Icon
    const auto *right_doc = m_controller.get_document(*m_split_document_index);
    if (right_doc != nullptr) {
      const std::string right_filename{right_doc->get_file_name()};
      const std::string right_icon = UI::Editor::file_icon_asset_for_path(
          std::filesystem::path{right_filename});
      const int right_icon_sz = std::max(round_to_int(13.0F * scale), 11);
      const float right_icon_cx =
          right_header.x + 12.0F * scale + right_icon_sz * 0.5F;
      surface.draw_svg_icon(context, "Assets/icons/" + right_icon,
                            round_to_int(right_icon_cx), round_to_int(center_y),
                            right_icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background, true);

      if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, right_filename,
                          right_header.x + 12.0F * scale + right_icon_sz +
                              6.0F * scale,
                          center_y, surface.m_text.primary);
      }
    }
  } else {
    m_split_close_btn_bounds = UI::Rect{};
    const float actions_right = header_bounds.right() - 4.0F * scale;
    m_tab_action_bounds[3] =
        UI::Rect{actions_right - 1.0F * btn_w, btn_y, btn_w, btn_h};
    m_tab_action_bounds[2] = UI::Rect{
        actions_right - 2.0F * btn_w - 2.0F * scale, btn_y, btn_w, btn_h};
    m_tab_action_bounds[1] = UI::Rect{
        actions_right - 3.0F * btn_w - 4.0F * scale, btn_y, btn_w, btn_h};
    m_tab_action_bounds[0] = UI::Rect{
        actions_right - 4.0F * btn_w - 6.0F * scale, btn_y, btn_w, btn_h};

    // Left side: File title with Icon
    if (document != nullptr) {
      const std::string filename{document->get_file_name()};
      const std::string icon_asset =
          UI::Editor::file_icon_asset_for_path(std::filesystem::path{filename});
      const int icon_sz = std::max(round_to_int(13.0F * scale), 11);
      const float icon_cx = header_bounds.x + 12.0F * scale + icon_sz * 0.5F;
      surface.draw_svg_icon(context, "Assets/icons/" + icon_asset,
                            round_to_int(icon_cx), round_to_int(center_y),
                            icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background, true);

      if (surface.m_small_font) {
        surface.draw_text(context, *surface.m_small_font, filename,
                          header_bounds.x + 12.0F * scale + icon_sz +
                              6.0F * scale,
                          center_y, surface.m_text.primary);
      }
    }
  }

  const char *icons[] = {
      "Assets/icons/split-right.svg",
      "Assets/icons/arrow-left.svg",
      "Assets/icons/arrow-right.svg",
      "Assets/icons/ellipsis.svg",
  };
  const int icon_sizes[] = {
      std::max(round_to_int(13.0F * scale), 11),
      std::max(round_to_int(12.0F * scale), 10),
      std::max(round_to_int(12.0F * scale), 10),
      std::max(round_to_int(13.0F * scale), 11),
  };

  for (std::size_t i = 0; i < 4; ++i) {
    const UI::Rect &btn = m_tab_action_bounds[i];
    const bool is_hovered =
        (m_hovered_tab_action && *m_hovered_tab_action == i);
    const bool is_active_menu =
        (i == 3 && m_tab_action_menu.visible) || (i == 0 && m_is_split);

    if (is_active_menu || is_hovered) {
      surface.fill_rounded_rectangle(
          context, btn, surface.m_colors.hover_background, 3.0F * scale);
    }

    surface.draw_svg_icon(
        context, icons[i], round_to_int(btn.x + btn.width * 0.5F),
        round_to_int(center_y), icon_sizes[i],
        (is_active_menu || is_hovered) ? surface.m_palette.text_primary
                                       : surface.m_palette.text_muted,
        (is_active_menu || is_hovered) ? surface.m_palette.hover_background
                                       : surface.m_palette.editor_background,
        false);
  }
}

void TextEditor::show_tab_action_menu(
    const UI::Editor::StudioEditorLayoutResult &layout) {
  const float btn_right = (!m_tab_action_bounds[3].is_empty())
                              ? m_tab_action_bounds[3].right()
                              : layout.editor_header_bounds.right();
  const float btn_bottom = (!m_tab_action_bounds[3].is_empty())
                               ? m_tab_action_bounds[3].bottom()
                               : layout.editor_header_bounds.bottom();

  NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Editor Actions"];
  [menu setAutoenablesItems:NO];

  ZDETabActionMenuHandler *handler = [[ZDETabActionMenuHandler alloc] init];
  handler.editor = this;

  auto add_item = [&](NSString *title, SEL action, NSString *key_equiv,
                      NSEventModifierFlags mods) {
    NSMenuItem *item =
        [[NSMenuItem alloc] initWithTitle:title
                                   action:action
                            keyEquivalent:key_equiv ? key_equiv : @""];
    item.target = handler;
    if (key_equiv && [key_equiv length] > 0) {
      item.keyEquivalentModifierMask = mods;
    }
    [menu addItem:item];
    [item release];
  };

  add_item(@"Split Up", @selector(onSplitUp:), @"\\",
           NSEventModifierFlagCommand);
  add_item(@"Split Down", @selector(onSplitDown:), @"\\",
           NSEventModifierFlagCommand);
  add_item(@"Split Left", @selector(onSplitLeft:), @"\\",
           NSEventModifierFlagCommand);
  add_item(@"Split Right", @selector(onSplitRight:), @"\\",
           NSEventModifierFlagCommand);
  [menu addItem:[NSMenuItem separatorItem]];
  add_item(@"Close All Editors", @selector(onCloseAll:), @"w",
           NSEventModifierFlagCommand | NSEventModifierFlagOption);
  add_item(@"Close Saved Editors", @selector(onCloseSaved:), @"u",
           NSEventModifierFlagCommand | NSEventModifierFlagOption);

  NSView *view = [NSApp keyWindow].contentView;
  if (view) {
    NSPoint loc = NSMakePoint(btn_right - 140.0f, btn_bottom + 2.0f);
    [menu popUpMenuPositioningItem:nil atLocation:loc inView:view];
  }

  [menu release];
  [handler release];
}

void TextEditor::draw_tab_action_menu(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  static_cast<void>(surface);
  static_cast<void>(context);
  static_cast<void>(layout);
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Editor::TextDocumentModel *document = get_focused_document();
  if (document == nullptr) {
    draw_empty_state(surface, context, layout);
    return;
  }

  const float scale = surface.m_dpi_scale;
  const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
  const float minimap_w = FIXED_MINIMAP_WIDTH * scale;

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    // Left Pane Layout
    const UI::Rect left_gutter = layout.gutter_bounds;
    const UI::Rect left_code{
        layout.editor_bounds.x, layout.editor_bounds.y,
        std::max(0.0F, splitter_x - layout.editor_bounds.x),
        layout.editor_bounds.height};
    const UI::Rect left_scrollbar{splitter_x - scrollbar_w,
                                  layout.editor_bounds.y, scrollbar_w,
                                  layout.editor_bounds.height};
    const UI::Rect left_minimap =
        (left_code.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? UI::Rect{left_scrollbar.x - minimap_w, layout.editor_bounds.y,
                       minimap_w, layout.editor_bounds.height}
            : UI::Rect{};

    UI::Editor::StudioEditorLayoutResult left_layout = layout;
    left_layout.gutter_bounds = left_gutter;
    left_layout.editor_bounds = left_code;
    left_layout.scrollbar_bounds = left_scrollbar;
    left_layout.minimap_bounds = left_minimap;

    // Right Pane Layout
    const float right_gutter_w = layout.gutter_bounds.width;
    const UI::Rect right_gutter{splitter_x + 2.0F * scale,
                                layout.editor_bounds.y, right_gutter_w,
                                layout.editor_bounds.height};
    const UI::Rect right_code{
        right_gutter.right(), layout.editor_bounds.y,
        std::max(0.0F, layout.editor_bounds.right() - right_gutter.right()),
        layout.editor_bounds.height};
    const UI::Rect right_scrollbar{layout.editor_bounds.right() - scrollbar_w,
                                   layout.editor_bounds.y, scrollbar_w,
                                   layout.editor_bounds.height};
    const UI::Rect right_minimap =
        (right_code.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? UI::Rect{right_scrollbar.x - minimap_w, layout.editor_bounds.y,
                       minimap_w, layout.editor_bounds.height}
            : UI::Rect{};

    UI::Editor::StudioEditorLayoutResult right_layout = layout;
    right_layout.gutter_bounds = right_gutter;
    right_layout.editor_bounds = right_code;
    right_layout.scrollbar_bounds = right_scrollbar;
    right_layout.minimap_bounds = right_minimap;

    const UI::Editor::TextDocumentModel *left_doc =
        m_controller.get_active_document();
    const UI::Editor::TextDocumentModel *right_doc =
        m_controller.get_document(*m_split_document_index);

    if (left_doc) {
      render_pane(surface, context, left_layout, left_doc, left_gutter,
                  left_code, false);
    }

    if (right_doc) {
      render_pane(surface, context, right_layout, right_doc, right_gutter,
                  right_code, true);
    }

    // Splitter Divider Bar on TOP of all pane components (full height from tab bar to editor bottom)
    const bool splitter_active = m_hovered_split_resize || m_is_resizing_split;
    const float divider_top_y = (!layout.editor_header_bounds.is_empty() && layout.editor_header_bounds.height > 2.0F)
                                    ? layout.editor_header_bounds.y
                                    : layout.tab_bar_bounds.y;
    const float divider_total_h = layout.editor_bounds.bottom() - divider_top_y;
    surface.fill_rectangle(context,
                           UI::Rect{splitter_x, divider_top_y,
                                    2.0F * scale, divider_total_h},
                           splitter_active ? surface.m_colors.accent
                                           : surface.m_colors.border);
  } else {
    render_pane(surface, context, layout, document, layout.gutter_bounds,
                layout.editor_bounds, false);
  }

  draw_split_drop_overlay(surface, context, layout);
}

void TextEditor::draw_split_drop_overlay(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  if (!m_tab_drag_drop.is_dragging() ||
      m_active_drop_zone == SplitDropZone::NoneZone) {
    return;
  }

  UI::Rect overlay_rect{};
  switch (m_active_drop_zone) {
  case SplitDropZone::Left:
    overlay_rect = {layout.editor_bounds.x, layout.editor_bounds.y,
                    layout.editor_bounds.width * 0.5F,
                    layout.editor_bounds.height};
    break;
  case SplitDropZone::Right:
    overlay_rect = {layout.editor_bounds.x + layout.editor_bounds.width * 0.5F,
                    layout.editor_bounds.y, layout.editor_bounds.width * 0.5F,
                    layout.editor_bounds.height};
    break;
  case SplitDropZone::Top:
    overlay_rect = {layout.editor_bounds.x, layout.editor_bounds.y,
                    layout.editor_bounds.width,
                    layout.editor_bounds.height * 0.5F};
    break;
  case SplitDropZone::Bottom:
    overlay_rect = {layout.editor_bounds.x,
                    layout.editor_bounds.y + layout.editor_bounds.height * 0.5F,
                    layout.editor_bounds.width,
                    layout.editor_bounds.height * 0.5F};
    break;
  default:
    break;
  }

  if (!overlay_rect.is_empty()) {
    const CGFloat fill_color[4] = {0.21, 0.52, 0.89, 0.25};
    const CGFloat border_color[4] = {0.21, 0.52, 0.89, 0.90};
    surface.fill_rounded_rectangle(context, overlay_rect, fill_color,
                                   4.0F * layout.dpi_scale);
    surface.draw_rectangle(context, overlay_rect, border_color);
  }
}

void TextEditor::render_pane(const StudioWorkspaceRenderer &surface,
                             CGContextRef context,
                             const UI::Editor::StudioEditorLayoutResult &layout,
                             const UI::Editor::TextDocumentModel *document,
                             const UI::Rect &gutter_rect,
                             const UI::Rect &code_rect,
                             bool is_split_pane) const {
  if (document == nullptr) {
    return;
  }

  const float dpi = surface.m_dpi_scale;
  auto &folding = is_split_pane ? m_split_folding : m_folding;
  auto &scrollbar = is_split_pane ? m_split_scrollbar : m_scrollbar;
  auto &minimap = is_split_pane ? m_split_minimap : m_minimap;

  const float line_height = 20.0F * dpi;
  const float first_center_y = code_rect.y + line_height * 0.5F;
  const float code_x =
      code_rect.x + 14.0F * dpi - (is_split_pane ? 0.0F : m_text_scroll_offset);
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(code_rect.height / line_height), 1));
  const std::size_t total_lines = document->get_line_count();

  const std::size_t tab_size = document->get_status().indent_width > 0
                                   ? document->get_status().indent_width
                                   : 4;

  folding.rebuild(std::vector<std::string>(document->get_lines().begin(),
                                           document->get_lines().end()),
                  tab_size);

  scrollbar.synchronize(count_visible_lines(folding, total_lines),
                        visible_count);
  const bool is_focused_pane =
      is_split_pane
          ? (m_focused_pane == SplitPaneFocus::Right)
          : (!m_is_split || m_focused_pane == SplitPaneFocus::Left);

  if (m_reveal_caret_pending && is_focused_pane) {
    static_cast<void>(scrollbar.reveal_line(physical_line_to_visual_row(
        folding, document->get_caret_line(), total_lines)));
    m_reveal_caret_pending = false;
  }
  const std::size_t first_visual_row = scrollbar.get_first_visible_line();
  const std::size_t first_line =
      visual_row_to_physical_line(folding, first_visual_row, total_lines);
  const bool syntax_highlighting =
      UI::Editor::supports_editor_syntax_highlighting(
          document->get_file_name());

  // 1. Fill solid gutter background
  surface.fill_rectangle(context, gutter_rect,
                         surface.m_colors.editor_background);

  // 2. Gutter separator + indent guides (VS Code style)
  {
    const float fold_margin =
        UI::Editor::StudioEditorMetrics::fold_margin_width * dpi;
    const float gutter_line_x = gutter_rect.right() - fold_margin - 1.0F;
    surface.draw_line(context, round_to_int(gutter_line_x),
                      round_to_int(gutter_rect.y), round_to_int(gutter_line_x),
                      round_to_int(gutter_rect.bottom()),
                      surface.m_colors.border);

    const float space_width =
        static_cast<float>(surface.m_editor_font->getTextWidth(" "));
    const UI::Components::ActiveIndentScope active_scope =
        folding.get_active_indent_scope(document->get_caret_line(), tab_size);

    std::size_t row_guide = 0;
    for (std::size_t line_index = first_line;
         row_guide < visible_count && line_index < total_lines; ++line_index) {
      if (folding.is_line_hidden(line_index)) {
        continue;
      }
      const float center_y =
          first_center_y + static_cast<float>(row_guide) * line_height;
      ++row_guide;

      const std::size_t line_indent = folding.get_effective_indent(line_index);
      if (line_indent < tab_size) {
        continue;
      }

      const float y_top = center_y - line_height * 0.5F;
      const float y_bottom = center_y + line_height * 0.5F;

      for (std::size_t col = tab_size; col <= line_indent; col += tab_size) {
        const float guide_x = code_x + static_cast<float>(col) * space_width;
        if (guide_x < code_rect.x || guide_x > code_rect.right()) {
          continue;
        }

        const bool is_active = active_scope.valid &&
                               col == active_scope.column &&
                               line_index >= active_scope.start_line &&
                               line_index <= active_scope.end_line;

        surface.draw_line(context, round_to_int(guide_x), round_to_int(y_top),
                          round_to_int(guide_x), round_to_int(y_bottom),
                          is_active ? surface.m_colors.indent_guide_active
                                    : surface.m_colors.indent_guide);
      }
    }
  }

  // Pass 1: Gutter line numbers and active line background
  std::size_t row_pass1 = 0;
  for (std::size_t line_index = first_line;
       row_pass1 < visible_count && line_index < total_lines; ++line_index) {
    if (folding.is_line_hidden(line_index)) {
      continue;
    }
    const std::string_view line = document->get_line(line_index);
    const float center_y =
        first_center_y + static_cast<float>(row_pass1) * line_height;
    ++row_pass1;
    const bool active_line =
        (line_index == document->get_caret_line()) &&
        (!m_is_split ||
         (is_split_pane ? (m_focused_pane == SplitPaneFocus::Right)
                        : (m_focused_pane == SplitPaneFocus::Left)));
    if (active_line && !document->has_selection()) {
      const float reserved_right =
          layout.scrollbar_bounds.width + layout.minimap_bounds.width;
      const float active_line_w =
          std::max(0.0F, layout.editor_bounds.width - reserved_right);
      surface.fill_rectangle(
          context,
          UI::Rect{gutter_rect.x, center_y - line_height * 0.5F,
                   (layout.editor_bounds.x + active_line_w) - gutter_rect.x,
                   line_height},
          surface.m_colors.active_line_background);
    }

    const float fold_margin =
        UI::Editor::StudioEditorMetrics::fold_margin_width * dpi;
    const std::string number = std::to_string(line_index + 1);
    const float number_x =
        gutter_rect.right() - fold_margin - 4.0F * dpi -
        static_cast<float>(surface.m_editor_font->getTextWidth(number));
    surface.draw_text(
        context, *surface.m_editor_font, number, number_x, center_y,
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

      const float dot_x = gutter_rect.x + 4.0F * dpi;
      const float dot_r = 3.0F * dpi;
      const CGFloat dot_error[4] = {247.0 / 255.0, 84.0 / 255.0, 100.0 / 255.0,
                                    1.0};
      const CGFloat dot_warn[4] = {240.0 / 255.0, 167.0 / 255.0, 50.0 / 255.0,
                                   1.0};
      const CGFloat dot_info[4] = {86.0 / 255.0, 182.0 / 255.0, 194.0 / 255.0,
                                   1.0};
      const CGFloat *dot_color =
          has_error ? dot_error : (has_warn ? dot_warn : dot_info);
      surface.fill_rounded_rectangle(
          context,
          UI::Rect{dot_x, center_y - dot_r, dot_r * 2.0F, dot_r * 2.0F},
          dot_color, dot_r);
    }

    if (has_gutter_marker(line)) {
      const int marker_x = round_to_int(gutter_rect.right() - 13.0F * dpi);
      const int marker_y = round_to_int(center_y);
      const int half = std::max(round_to_int(3.0F * dpi), 2);
      surface.draw_line(context, marker_x, marker_y - half, marker_x + half,
                        marker_y, surface.m_colors.text_muted);
      surface.draw_line(context, marker_x + half, marker_y, marker_x,
                        marker_y + half, surface.m_colors.text_muted);
      surface.draw_line(context, marker_x, marker_y + half, marker_x - half,
                        marker_y, surface.m_colors.text_muted);
      surface.draw_line(context, marker_x - half, marker_y, marker_x,
                        marker_y - half, surface.m_colors.text_muted);
    }

    // --- Fold icon and scope guide rendering ---
    const UI::Components::FoldMarker fold_marker =
        folding.get_marker(line_index);
    const float fold_center_x = gutter_rect.right() - fold_margin * 0.5F;
    const int fold_cx = round_to_int(fold_center_x);
    const int fold_cy = round_to_int(center_y);

    if (fold_marker == UI::Components::FoldMarker::Expanded ||
        fold_marker == UI::Components::FoldMarker::Collapsed) {
      const bool fold_hovered =
          m_hovered_fold_line && *m_hovered_fold_line == line_index;
      const int box_half = std::max(round_to_int(5.0F * dpi), 4);
      surface.fill_rectangle(context,
                             UI::Rect{static_cast<float>(fold_cx - box_half),
                                      static_cast<float>(fold_cy - box_half),
                                      static_cast<float>(box_half * 2),
                                      static_cast<float>(box_half * 2)},
                             active_line
                                 ? surface.m_colors.active_line_background
                                 : surface.m_colors.editor_background);
      surface.draw_rectangle(context,
                             UI::Rect{static_cast<float>(fold_cx - box_half),
                                      static_cast<float>(fold_cy - box_half),
                                      static_cast<float>(box_half * 2),
                                      static_cast<float>(box_half * 2)},
                             fold_hovered ? surface.m_colors.accent
                                          : surface.m_colors.border);

      const int sign_inset = std::max(round_to_int(2.0F * dpi), 2);
      surface.draw_line(context, fold_cx - box_half + sign_inset, fold_cy,
                        fold_cx + box_half - sign_inset, fold_cy,
                        fold_hovered ? surface.m_colors.accent
                                     : surface.m_colors.text_muted);
      if (fold_marker == UI::Components::FoldMarker::Collapsed) {
        surface.draw_line(context, fold_cx, fold_cy - box_half + sign_inset,
                          fold_cx, fold_cy + box_half - sign_inset,
                          fold_hovered ? surface.m_colors.accent
                                       : surface.m_colors.text_muted);
      }
    } else if (fold_marker == UI::Components::FoldMarker::Continuation) {
      surface.draw_line(context, fold_cx,
                        round_to_int(center_y - line_height * 0.5F), fold_cx,
                        round_to_int(center_y + line_height * 0.5F),
                        surface.m_colors.border);
    } else if (fold_marker == UI::Components::FoldMarker::End) {
      surface.draw_line(context, fold_cx,
                        round_to_int(center_y - line_height * 0.5F), fold_cx,
                        fold_cy, surface.m_colors.border);
      surface.draw_line(context, fold_cx, fold_cy,
                        round_to_int(gutter_rect.right() - 2.0F * dpi), fold_cy,
                        surface.m_colors.border);
    }

    if (fold_marker == UI::Components::FoldMarker::Expanded) {
      const int box_half = std::max(round_to_int(5.0F * dpi), 4);
      surface.draw_line(context, fold_cx, fold_cy + box_half, fold_cx,
                        round_to_int(center_y + line_height * 0.5F),
                        surface.m_colors.border);
    }
  }

  // Pass 2: Selection (animated) + text rendering with clipping
  const float reserved_right =
      layout.scrollbar_bounds.width + layout.minimap_bounds.width;
  const float text_clip_w =
      std::max(0.0F, layout.editor_bounds.width - reserved_right);
  const float hscroll_height =
      (!m_is_split && m_max_text_scroll > 0.0f) ? 14.0F * dpi : 0.0f;
  surface.push_clip(
      context,
      UI::Rect{layout.editor_bounds.x, layout.editor_bounds.y, text_clip_w,
               std::max(0.0f, layout.editor_bounds.height - hscroll_height)});
  // Selection Highlight: Direct & Instant (0 animation delay)
  const bool is_this_pane_focused =
      !m_is_split || (is_split_pane ? (m_focused_pane == SplitPaneFocus::Right)
                                    : (m_focused_pane == SplitPaneFocus::Left));

  if (is_this_pane_focused) {
    for (const auto &cursor : document->get_all_cursors()) {
      if (cursor.has_selection()) {
        const UI::Editor::TextSelection selection = cursor.get_selection();
        const std::size_t start_line =
            std::max(selection.start.line, first_line);
        const std::size_t end_line =
            std::min(selection.end.line, first_line + visible_count);

        for (std::size_t line_index = start_line;
             line_index <= end_line && line_index < total_lines; ++line_index) {
          if (folding.is_line_hidden(line_index) ||
              line_index >= document->get_line_count()) {
            continue;
          }
          const std::string_view line = document->get_line(line_index);
          const std::size_t selection_start =
              line_index == selection.start.line ? selection.start.column : 0;
          const std::size_t selection_end = line_index == selection.end.line
                                                ? selection.end.column
                                                : line.size();

          const float selection_x =
              static_cast<float>(surface.m_editor_font->getTextWidth(
                  std::string{line.substr(0, selection_start)}));
          float selection_width = static_cast<float>(
              surface.m_editor_font->getTextWidth(std::string{line.substr(
                  selection_start, selection_end - selection_start)}));

          if (line_index < selection.end.line) {
            selection_width += 6.0F * dpi;
          }

          if (selection_width <= 0.0F) {
            continue;
          }

          const std::size_t visual_row =
              physical_line_to_visual_row(folding, line_index, total_lines);
          const float screen_y =
              layout.editor_bounds.y + (static_cast<float>(visual_row) -
                                        static_cast<float>(first_visual_row)) *
                                           line_height;
          const float screen_x = code_x + selection_x;

          if (screen_y + line_height >= layout.editor_bounds.y &&
              screen_y <= layout.editor_bounds.bottom()) {
            const int snap_y = round_to_int(screen_y);
            const int snap_bottom = round_to_int(screen_y + line_height);
            const int snap_x = round_to_int(screen_x);
            const int snap_right = round_to_int(screen_x + selection_width);
            surface.fill_rounded_rectangle(
                context,
                UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y),
                         static_cast<float>(snap_right - snap_x),
                         static_cast<float>(snap_bottom - snap_y)},
                surface.m_colors.selection_background, 4.0F * dpi);
          }
        }
      }
    }
  }

  float max_line_width = 0.0F;
  std::size_t row_pass2 = 0;
  for (std::size_t line_index = first_line;
       row_pass2 < visible_count && line_index < total_lines; ++line_index) {
    if (folding.is_line_hidden(line_index)) {
      continue;
    }
    const std::string_view line = document->get_line(line_index);
    const float center_y =
        first_center_y + static_cast<float>(row_pass2) * line_height;
    ++row_pass2;

    const float current_line_width = static_cast<float>(
        surface.m_editor_font->getTextWidth(std::string{line}));
    max_line_width = std::max(max_line_width, current_line_width);

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

    const auto line_diags = document->get_diagnostics_for_line(line_index);
    if (syntax_highlighting) {
      float token_x = code_x;
      std::size_t rendered_bytes = 0;
      std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
          tokens{};
      auto line_state = document->get_line_state(line_index);
      const std::size_t token_count = UI::Editor::tokenize_editor_line(
          line, tokens, document->get_file_name(), &line_state);
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
            std::string_view pre = token.text.substr(0, brace_offset);
            surface.draw_text(context, *surface.m_editor_font, pre, token_x,
                              center_y, token_color(token.kind));
            token_x += static_cast<float>(
                surface.get_text_width(context, *surface.m_editor_font, pre));
          }

          std::string_view brace_char = token.text.substr(brace_offset, 1);
          float pulse = m_brace_animation.get_pulse_scale();
          float brace_w = static_cast<float>(surface.get_text_width(
              context, *surface.m_editor_font, brace_char));
          float extra_w = (brace_w * pulse - brace_w) * 0.5F;
          float extra_h = (line_height * pulse - line_height) * 0.5F;
          float screen_y = center_y - line_height * 0.5F;

          UI::Theme::Color pulse_color = surface.m_palette.selection_background;
          pulse_color.red = std::min(pulse_color.red + 30, 255);
          pulse_color.green = std::min(pulse_color.green + 30, 255);
          pulse_color.blue = std::min(pulse_color.blue + 30, 255);

          surface.fill_rounded_rectangle(
              context,
              UI::Rect{token_x - extra_w - 2.0F, screen_y - extra_h,
                       brace_w + extra_w * 2.0F + 4.0F,
                       line_height + extra_h * 2.0F},
              pulse_color, 3.0F * surface.m_dpi_scale * pulse);

          surface.draw_scaled_text(context, *surface.m_editor_font, brace_char,
                                   token_x, center_y, pulse,
                                   surface.m_text.accent);
          token_x += brace_w;

          if (brace_offset + 1 < token.text.size()) {
            std::string_view post = token.text.substr(brace_offset + 1);
            surface.draw_text(context, *surface.m_editor_font, post, token_x,
                              center_y, token_color(token.kind));
            token_x += static_cast<float>(
                surface.get_text_width(context, *surface.m_editor_font, post));
          }
        } else {
          surface.draw_text(context, *surface.m_editor_font, token.text, token_x,
                            center_y, token_color(token.kind));
          token_x += static_cast<float>(surface.get_text_width(
              context, *surface.m_editor_font, token.text));
        }
        rendered_bytes += token.text.size();
      }
      if (rendered_bytes < line.size()) {
        surface.draw_text(context, *surface.m_editor_font,
                          line.substr(rendered_bytes), token_x, center_y,
                          surface.m_text.primary);
      }
    } else {
      surface.draw_text(context, *surface.m_editor_font, line, code_x, center_y,
                        surface.m_text.primary);
    }

    // Render diagnostics squiggles under erroneous tokens
    for (const auto &diag : line_diags) {
      std::size_t start_col =
          diag.range.start.line == line_index ? diag.range.start.character : 0;
      std::size_t end_col = diag.range.end.line == line_index
                                ? diag.range.end.character
                                : line.size();
      if (end_col > line.size())
        end_col = line.size();
      if (start_col > line.size())
        start_col = 0;

      // Smart include range expansion for header path diagnostics
      if (line.find("#include") != std::string::npos) {
        const std::size_t first_quote = line.find_first_of("\"<");
        const std::size_t last_quote = line.find_last_of("\">");
        if (first_quote != std::string::npos &&
            last_quote != std::string::npos && last_quote > first_quote) {
          if (start_col <= first_quote || (end_col - start_col) <= 2) {
            start_col = first_quote;
            end_col = last_quote + 1;
          }
        }
      }

      if (start_col >= end_col) {
        start_col = 0;
        end_col = line.size();
      }

      float diag_start_x = code_x;
      if (start_col > 0 && start_col <= line.size()) {
        diag_start_x += static_cast<float>(surface.m_editor_font->getTextWidth(
            std::string{line.substr(0, start_col)}));
      }
      float diag_width = 8.0F;
      if (end_col > start_col && start_col < line.size()) {
        diag_width = static_cast<float>(surface.m_editor_font->getTextWidth(
            std::string{line.substr(start_col, end_col - start_col)}));
      }

      const CGFloat squiggle_error[4] = {247.0 / 255.0, 84.0 / 255.0,
                                         100.0 / 255.0, 1.0};
      const CGFloat squiggle_warn[4] = {240.0 / 255.0, 167.0 / 255.0,
                                        50.0 / 255.0, 1.0};
      const CGFloat squiggle_info[4] = {86.0 / 255.0, 182.0 / 255.0,
                                        194.0 / 255.0, 1.0};
      const CGFloat *squiggle_color =
          diag.severity == Language::Protocol::DiagnosticSeverity::Error
              ? squiggle_error
              : (diag.severity ==
                         Language::Protocol::DiagnosticSeverity::Warning
                     ? squiggle_warn
                     : squiggle_info);

      // Draw crisp sinusoidal wavy squiggle
      float wave_x = diag_start_x;
      const float wave_end_x = diag_start_x + std::max(diag_width, 6.0F);
      const float wave_y = center_y + line_height * 0.42F;
      const float wave_step = 3.0F * dpi;
      const float wave_amp = 1.5F * dpi;
      bool wave_up = true;
      while (wave_x < wave_end_x) {
        const float next_x = std::min(wave_x + wave_step, wave_end_x);
        const float y1 = wave_up ? (wave_y - wave_amp) : (wave_y + wave_amp);
        const float y2 = wave_up ? (wave_y + wave_amp) : (wave_y - wave_amp);
        surface.draw_line(context, round_to_int(wave_x), round_to_int(y1),
                          round_to_int(next_x), round_to_int(y2),
                          squiggle_color);
        wave_x = next_x;
        wave_up = !wave_up;
      }
    }

    // Render Inline Error Lens (Inspection hint at end of line)
    if (!line_diags.empty()) {
      const auto *top_diag = &line_diags[0];
      for (const auto &d : line_diags) {
        if (d.severity < top_diag->severity) {
          top_diag = &d;
        }
      }

      std::string badge_prefix = "   x  ";
      std::string badge_fg = "#f75464";
      const CGFloat lens_error_bg[4] = {48.0 / 255.0, 20.0 / 255.0,
                                        24.0 / 255.0, 180.0 / 255.0};
      const CGFloat lens_warn_bg[4] = {48.0 / 255.0, 38.0 / 255.0, 20.0 / 255.0,
                                       180.0 / 255.0};
      const CGFloat lens_info_bg[4] = {20.0 / 255.0, 36.0 / 255.0, 48.0 / 255.0,
                                       180.0 / 255.0};
      const CGFloat *badge_bg = lens_error_bg;

      if (top_diag->severity ==
          Language::Protocol::DiagnosticSeverity::Warning) {
        badge_prefix = "   !  ";
        badge_fg = "#f0a732";
        badge_bg = lens_warn_bg;
      } else if (top_diag->severity >=
                 Language::Protocol::DiagnosticSeverity::Information) {
        badge_prefix = "   i  ";
        badge_fg = "#56b6c2";
        badge_bg = lens_info_bg;
      }

      std::string hint_text = badge_prefix + top_diag->message;
      if (hint_text.size() > 90) {
        hint_text = hint_text.substr(0, 87) + "...";
      }

      const float hint_x = code_x + current_line_width + 20.0F * dpi;
      const int hint_w = surface.m_ui_font->getTextWidth(hint_text);
      const UI::Rect lens_rect{
          hint_x - 6.0F * dpi, center_y - line_height * 0.4F,
          static_cast<float>(hint_w) + 12.0F * dpi, line_height * 0.8F};

      surface.fill_rounded_rectangle(context, lens_rect, badge_bg, 3.0F * dpi);
      surface.draw_text(context, *surface.m_ui_font, hint_text, hint_x,
                        center_y, badge_fg, &lens_rect);
    }

    // Caret
    const bool is_this_pane_focused =
        !m_is_split ||
        (is_split_pane ? (m_focused_pane == SplitPaneFocus::Right)
                       : (m_focused_pane == SplitPaneFocus::Left));
    if (m_focused && is_this_pane_focused && m_caret_blink.is_visible()) {
      for (const auto &cur : document->get_all_cursors()) {
        if (cur.line == line_index) {
          const std::string_view prefix =
              line.substr(0, std::min(cur.column, line.size()));
          const int caret_x = round_to_int(
              code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                           std::string{prefix})));
          const float caret_w = std::max(2.0F * dpi, 1.5F);
          surface.fill_rectangle(context,
                                 UI::Rect{static_cast<float>(caret_x),
                                          center_y - 8.5F * dpi, caret_w,
                                          17.0F * dpi},
                                 surface.m_colors.text_primary);
        }
      }
    }
  }

  surface.pop_clip(context);

  // Horizontal scroll thumb (single pane only)
  if (!m_is_split) {
    const float content_width = max_line_width + 28.0F * dpi;
    const float new_max_scroll = std::max(0.0F, content_width - text_clip_w);
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
      const float track_width = text_clip_w;
      const float track_height = 14.0F * dpi;
      const float track_y = layout.editor_bounds.bottom() - track_height;
      const float thumb_width =
          std::max(20.0F * dpi, track_width * (track_width / content_width));
      const float thumb_x =
          layout.editor_bounds.x + (m_text_scroll_offset / m_max_text_scroll) *
                                       (track_width - thumb_width);
      const float thumb_height = 6.0F * dpi;
      surface.fill_rectangle(
          context,
          UI::Rect{thumb_x, track_y + (track_height - thumb_height) * 0.5F,
                   thumb_width, thumb_height},
          surface.m_colors.text_muted);
    }
  } else {
    const_cast<TextEditor *>(this)->m_text_scroll_offset = 0.0F;
  }

  // Render scrollbar
  scrollbar.render(surface, context, layout);

  // Scrollbar Overview Ruler stripes (Overview Ruler)
  if (document && total_lines > 0 && !layout.scrollbar_bounds.is_empty()) {
    const float track_x = layout.scrollbar_bounds.x;
    const float track_y = layout.scrollbar_bounds.y;
    const float track_w = layout.scrollbar_bounds.width;
    const float track_h = layout.scrollbar_bounds.height;

    for (std::size_t line_idx = 0; line_idx < total_lines; ++line_idx) {
      const auto diags = document->get_diagnostics_for_line(line_idx);
      if (diags.empty())
        continue;

      bool has_err = false;
      bool has_warn = false;
      for (const auto &d : diags) {
        if (d.severity == Language::Protocol::DiagnosticSeverity::Error)
          has_err = true;
        else if (d.severity == Language::Protocol::DiagnosticSeverity::Warning)
          has_warn = true;
      }

      const float stripe_y = track_y + (static_cast<float>(line_idx) /
                                        static_cast<float>(total_lines)) *
                                           track_h;
      const CGFloat dot_error[4] = {247.0 / 255.0, 84.0 / 255.0, 100.0 / 255.0,
                                    1.0};
      const CGFloat dot_warn[4] = {240.0 / 255.0, 167.0 / 255.0, 50.0 / 255.0,
                                   1.0};
      const CGFloat dot_info[4] = {86.0 / 255.0, 182.0 / 255.0, 194.0 / 255.0,
                                   1.0};
      const CGFloat *stripe_color =
          has_err ? dot_error : (has_warn ? dot_warn : dot_info);

      surface.fill_rectangle(context,
                             UI::Rect{track_x + 1.0F, stripe_y, track_w - 2.0F,
                                      std::max(2.5F * dpi, 2.0F)},
                             stripe_color);
    }
  }

  if (document && !layout.minimap_bounds.is_empty()) {
    minimap.render(surface, context, layout, *document, first_line,
                   visible_count);
  }

  // Render signature help overlay
  std::lock_guard<std::mutex> lsp_lock(m_lsp_mutex);
  if (m_signature_help.is_visible() && document != nullptr) {
    const auto &help = m_signature_help.get_help();
    if (!help.signatures.empty()) {
      const auto &sig =
          help.signatures[help.active_signature < help.signatures.size()
                              ? help.active_signature
                              : 0];
      const std::string_view current_line =
          document->get_line(document->get_caret_line());
      const std::string_view prefix = current_line.substr(
          0, std::min(document->get_caret_column(), current_line.size()));
      const float caret_screen_x =
          code_x + static_cast<float>(surface.m_editor_font->getTextWidth(
                       std::string{prefix}));
      const float caret_line_top_y =
          layout.editor_bounds.y +
          static_cast<float>(
              physical_line_to_visual_row(m_folding, document->get_caret_line(),
                                          document->get_line_count()) -
              m_scrollbar.get_first_visible_line()) *
              (20.0F * dpi);

      const int sig_w = surface.m_ui_font->getTextWidth(sig.label);
      const float box_w = std::clamp(static_cast<float>(sig_w) + 24.0F * dpi,
                                     200.0F * dpi, 500.0F * dpi);
      const float box_h = 28.0F * dpi;
      const float box_x =
          std::clamp(caret_screen_x, layout.editor_bounds.x + 8.0F,
                     layout.editor_bounds.right() - box_w - 8.0F);
      const float box_y = caret_line_top_y - box_h - 4.0F * dpi;

      const UI::Rect box_rect{box_x, box_y, box_w, box_h};
      const CGFloat sig_bg[4] = {28.0 / 255.0, 28.0 / 255.0, 32.0 / 255.0,
                                 0.95};
      const CGFloat sig_border[4] = {65.0 / 255.0, 65.0 / 255.0, 72.0 / 255.0,
                                     1.0};
      surface.fill_rounded_rectangle(context, box_rect, sig_bg, 4.0F * dpi);
      surface.draw_rectangle(context, box_rect, sig_border);
      surface.draw_text(context, *surface.m_ui_font, sig.label,
                        box_x + 8.0F * dpi, box_y + box_h * 0.5F,
                        surface.m_text.primary, &box_rect);
    }
  }

  // Render completion popup overlay if active (Sleek Minimalist Modal Popup
  // Style)
  if (m_completion_popup.is_visible() &&
      m_completion_popup.get_item_count() > 0 && document != nullptr) {
    const std::string_view current_line =
        document->get_line(document->get_caret_line());
    const std::string_view prefix = current_line.substr(
        0, std::min(document->get_caret_column(), current_line.size()));
    const float caret_screen_x =
        code_x + static_cast<float>(
                     surface.m_editor_font->getTextWidth(std::string{prefix}));
    const float line_h = 20.0F * dpi;
    const float caret_line_y =
        layout.editor_bounds.y +
        static_cast<float>(
            physical_line_to_visual_row(m_folding, document->get_caret_line(),
                                        document->get_line_count()) -
            m_scrollbar.get_first_visible_line() + 1) *
            line_h;

    const float item_h = 24.0F * dpi;
    const float footer_h = 26.0F * dpi;
    const std::size_t count = m_completion_popup.get_item_count();
    const std::size_t scroll_offset = m_completion_popup.get_scroll_offset();
    const std::size_t max_visible =
        std::min<std::size_t>(m_completion_popup.get_max_visible_items(), 12);
    const std::size_t visible_count = std::min<std::size_t>(count, max_visible);
    const float popup_h =
        static_cast<float>(visible_count) * item_h + footer_h + 10.0F * dpi;

    // Calculate dynamic popup width
    float max_label_w = 280.0F * dpi;
    for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count;
         ++i) {
      if (const auto *it = m_completion_popup.get_item(scroll_offset + i)) {
        const int w = surface.m_editor_font->getTextWidth(it->label);
        max_label_w =
            std::max(max_label_w, static_cast<float>(w) + 80.0F * dpi);
      }
    }
    const float popup_w = std::clamp(max_label_w, 380.0F * dpi, 540.0F * dpi);
    const float popup_x =
        std::clamp(caret_screen_x, layout.editor_bounds.x + 8.0F,
                   std::max(layout.editor_bounds.x + 8.0F,
                            layout.editor_bounds.right() - (popup_w + 12.0F)));

    float popup_y = caret_line_y;
    if (popup_y + popup_h > layout.editor_bounds.bottom() - 10.0F) {
      popup_y = std::max(layout.editor_bounds.y + 4.0F,
                         caret_line_y - line_h - popup_h);
    }

    const UI::Rect actual_bounds{popup_x, popup_y, popup_w, popup_h};
    const float card_radius = 10.0F * dpi;

    // 1. Native macOS Cocoa Window/Popover Drop Shadow
    CGContextSaveGState(context);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    const CGFloat shadowColorComps[4] = {0.0, 0.0, 0.0, 0.55};
    CGColorRef cgShadowColor = CGColorCreate(colorSpace, shadowColorComps);
    CGContextSetShadowWithColor(context, CGSizeMake(0, -6.0 * dpi), 20.0 * dpi,
                                cgShadowColor);

    // 2. Native macOS Rounded Dark Card Fill (30, 31, 34)
    const NSRect nsCardRect =
        NSMakeRect(actual_bounds.x, actual_bounds.y, actual_bounds.width,
                   actual_bounds.height);
    NSBezierPath *bgPath = [NSBezierPath bezierPathWithRoundedRect:nsCardRect
                                                           xRadius:card_radius
                                                           yRadius:card_radius];
    [[NSColor colorWithSRGBRed:30.0 / 255.0
                         green:31.0 / 255.0
                          blue:34.0 / 255.0
                         alpha:0.98] setFill];
    [bgPath fill];

    CGColorRelease(cgShadowColor);
    CGColorSpaceRelease(colorSpace);
    CGContextRestoreGState(context);

    // 3. Native macOS Cocoa 1px Rounded Outer Border (58, 60, 68)
    CGContextSaveGState(context);
    NSBezierPath *strokePath = [NSBezierPath
        bezierPathWithRoundedRect:NSInsetRect(nsCardRect, 0.5, 0.5)
                          xRadius:card_radius
                          yRadius:card_radius];
    [[NSColor colorWithSRGBRed:58.0 / 255.0
                         green:60.0 / 255.0
                          blue:68.0 / 255.0
                         alpha:1.0] setStroke];
    [strokePath setLineWidth:1.0];
    [strokePath stroke];
    CGContextRestoreGState(context);

    const std::size_t selected = m_completion_popup.get_selected_index();
    const std::string &query = m_completion_popup.get_filter();

    // 4. Item Rows with Rounded 6.0px Selection Pills
    for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count;
         ++i) {
      const std::size_t item_idx = scroll_offset + i;
      const auto *item = m_completion_popup.get_item(item_idx);
      if (item == nullptr)
        continue;

      const float row_y =
          actual_bounds.y + 6.0F * dpi + static_cast<float>(i) * item_h;
      const float item_w =
          actual_bounds.width - (count > max_visible ? 14.0F : 10.0F) * dpi;
      const UI::Rect item_rect{actual_bounds.x + 5.0F * dpi, row_y, item_w,
                               item_h};

      if (item_idx == selected) {
        const NSRect itemNSRect = NSMakeRect(item_rect.x, item_rect.y,
                                             item_rect.width, item_rect.height);
        NSBezierPath *selPath =
            [NSBezierPath bezierPathWithRoundedRect:itemNSRect
                                            xRadius:6.0
                                            yRadius:6.0];
        [[NSColor colorWithSRGBRed:50.0 / 255.0
                             green:56.0 / 255.0
                              blue:68.0 / 255.0
                             alpha:0.95] setFill];
        [selPath fill];
      }

      // Minimalist Syntax Color
      std::string label_color = "#4fc1ff"; // Bright preprocessor/keyword blue
      if (item->kind == Language::Protocol::CompletionItemKind::Function ||
          item->kind == Language::Protocol::CompletionItemKind::Method) {
        label_color = "#dcdcaa";
      } else if (item->kind ==
                     Language::Protocol::CompletionItemKind::Variable ||
                 item->kind == Language::Protocol::CompletionItemKind::Field) {
        label_color = "#9cdcfe";
      } else if (item->kind == Language::Protocol::CompletionItemKind::Class ||
                 item->kind == Language::Protocol::CompletionItemKind::Struct ||
                 item->kind ==
                     Language::Protocol::CompletionItemKind::Interface) {
        label_color = "#4ec9b0";
      } else if (item->kind ==
                 Language::Protocol::CompletionItemKind::Snippet) {
        label_color = "#f59e0b";
      }

      const float text_x = item_rect.x + 12.0F * dpi;
      surface.draw_text(context, *surface.m_editor_font, item->label, text_x,
                        row_y + item_h * 0.5F, label_color, &item_rect);

      // Highlight matched query letters in bright white
      if (!query.empty() && item->label.size() >= query.size()) {
        std::string l_lower = item->label;
        std::string q_lower = query;
        for (char &c : l_lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char &c : q_lower)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto m_pos = l_lower.find(q_lower);
        if (m_pos != std::string::npos) {
          const int w_before =
              surface.m_editor_font->getTextWidth(item->label.substr(0, m_pos));
          const std::string matched_sub =
              item->label.substr(m_pos, query.size());
          surface.draw_text(context, *surface.m_editor_font, matched_sub,
                            text_x + static_cast<float>(w_before),
                            row_y + item_h * 0.5F, "#ffffff", &item_rect);
        }
      }

      // Right side detail / type hint
      if (!item->detail.empty()) {
        const int detail_w =
            surface.m_small_font
                ? surface.m_small_font->getTextWidth(item->detail)
                : 0;
        const float detail_x = std::max(
            item_rect.x + 220.0F * dpi,
            item_rect.right() - static_cast<float>(detail_w) - 10.0F * dpi);
        if (surface.m_small_font) {
          surface.draw_text(context, *surface.m_small_font, item->detail,
                            detail_x, row_y + item_h * 0.5F, "#888890",
                            &item_rect);
        }
      }
    }

    // 5. Minimalist Scrollbar Pill
    if (count > max_visible) {
      const float track_x = actual_bounds.right() - 5.0F * dpi;
      const float track_y = actual_bounds.y + 6.0F * dpi;
      const float track_h = static_cast<float>(visible_count) * item_h;
      const float thumb_h =
          std::max(14.0F * dpi, track_h * (static_cast<float>(max_visible) /
                                           static_cast<float>(count)));
      const float max_scroll = static_cast<float>(count - max_visible);
      const float thumb_y =
          track_y + (static_cast<float>(scroll_offset) / max_scroll) *
                        (track_h - thumb_h);

      const CGFloat popup_thumb_bg[4] = {110.0 / 255.0, 110.0 / 255.0,
                                         110.0 / 255.0, 0.6};
      const UI::Rect thumb_rect{track_x, thumb_y, 3.0F * dpi, thumb_h};
      surface.fill_rounded_rectangle(context, thumb_rect, popup_thumb_bg,
                                     1.5F * dpi);
    }

    // 6. Bottom Modal Footer (Tips & Help)
    const float footer_y = actual_bounds.bottom() - footer_h;
    const CGFloat footer_line_col[4] = {48.0 / 255.0, 50.0 / 255.0,
                                        55.0 / 255.0, 1.0};
    surface.draw_line(context, actual_bounds.x, footer_y, actual_bounds.right(),
                      footer_y, footer_line_col);

    if (surface.m_small_font) {
      const std::string tip_text = "Press ^. to choose the selected (or first) "
                                   "suggestion and insert a dot afterwards";
      surface.draw_text(context, *surface.m_small_font, tip_text,
                        actual_bounds.x + 12.0F * dpi,
                        footer_y + footer_h * 0.5F, "#888890");

      const int tip_w = surface.m_small_font->getTextWidth(tip_text);
      const float next_tip_x =
          actual_bounds.x + 18.0F * dpi + static_cast<float>(tip_w);
      if (next_tip_x + 60.0F * dpi < actual_bounds.right() - 24.0F * dpi) {
        surface.draw_text(context, *surface.m_small_font, "Next Tip",
                          next_tip_x, footer_y + footer_h * 0.5F, "#5384e4");
      }

      // Vertical 3-dots ⋮ on right
      surface.draw_text(context, *surface.m_small_font, "⋮",
                        actual_bounds.right() - 16.0F * dpi,
                        footer_y + footer_h * 0.5F, "#888890");
    }
  }
}

void TextEditor::draw_empty_state(
    const StudioWorkspaceRenderer &surface, CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  m_empty_state_open_btn.set_bounds(UI::Rect{});
  m_empty_state_clone_btn.set_bounds(UI::Rect{});

  const float dpi = surface.m_dpi_scale;
  const float logo_size = 180.0F * dpi;
  const float logo_gap = 32.0F * dpi;

  const std::string title = "Zenvra Development Studio 2026";
  const int title_w = surface.m_large_font
                          ? surface.m_large_font->getTextWidth(title)
                          : static_cast<int>(240.0F * dpi);
  const float text_block_w =
      std::max(static_cast<float>(title_w), 260.0F * dpi);
  const float total_w = logo_size + logo_gap + text_block_w;

  const float start_x = std::max(
      layout.editor_bounds.x + 30.0F * dpi,
      layout.editor_bounds.x + (layout.editor_bounds.width - total_w) * 0.5F);
  const float start_y =
      layout.editor_bounds.y + layout.editor_bounds.height * 0.32F;

  // 1. Extra Large Iconic Logo on the left
  surface.draw_png_image(context, "Assets/icons/zenvra_logo.png",
                         round_to_int(start_x + logo_size * 0.5F),
                         round_to_int(start_y + logo_size * 0.5F),
                         round_to_int(logo_size));

  const float text_x = start_x + logo_size + logo_gap;

  // 2. Heading "Zenvra Development Studio"
  if (surface.m_large_font) {
    surface.draw_text(context, *surface.m_large_font, title, text_x,
                      start_y + 36.0F * dpi, surface.m_text.primary);
  } else if (surface.m_ui_font) {
    surface.draw_text(context, *surface.m_ui_font, title, text_x,
                      start_y + 36.0F * dpi, surface.m_text.primary);
  }

  // 3. Shortcuts list aligned directly under the heading (uniform neutral
  // tones, no blue)
  if (surface.m_small_font || surface.m_ui_font) {
    auto &font =
        surface.m_small_font ? *surface.m_small_font : *surface.m_ui_font;
    struct ShortcutEntry {
      std::string_view key;
      std::string_view label;
    };
    static constexpr std::array<ShortcutEntry, 4> shortcuts{{
        {"Ctrl+O", "Open File"},
        {"Ctrl+Shift+P", "Command Palette"},
        {"Ctrl+`", "Toggle Terminal"},
        {"Ctrl+B", "Toggle Sidebar"},
    }};

    const float key_col_w = 110.0F * dpi;
    const float item_gap = 24.0F * dpi;
    const float first_row_y = start_y + 74.0F * dpi;

    for (std::size_t i = 0; i < shortcuts.size(); ++i) {
      const float row_y = first_row_y + static_cast<float>(i) * item_gap;
      surface.draw_text(context, font, shortcuts[i].key, text_x, row_y,
                        surface.m_text.primary);
      surface.draw_text(context, font, shortcuts[i].label, text_x + key_col_w,
                        row_y, surface.m_text.muted);
    }
  }
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float px,
    float py) const {
  const float dpi = surface.m_dpi_scale;
  const float line_height = 20.0F * dpi;

  const bool is_split =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * dpi) * m_split_ratio;

  if (is_split && m_focused_pane == SplitPaneFocus::Right) {
    const auto *split_doc = m_controller.get_document(*m_split_document_index);
    if (split_doc == nullptr)
      return {0, 0};

    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = split_doc->get_line_count();
    m_split_scrollbar.synchronize(
        count_visible_lines(m_split_folding, total_lines), visible_count);

    const std::size_t first_line = m_split_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        py, layout.editor_bounds.y,
        std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height),
        0));
    const std::size_t line_index = visual_row_to_physical_line(
        m_split_folding, first_line + clicked_row, total_lines);

    const std::string_view line = split_doc->get_line(line_index);
    const float right_x = splitter_x + 2.0F * dpi;
    const float right_gutter_w = layout.gutter_bounds.width;
    const float code_x = right_x + right_gutter_w + 14.0F * dpi;
    const float target_x = std::max(px - code_x, 0.0F);

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

  const auto *doc = m_controller.get_active_document();
  if (doc == nullptr) {
    return {0, 0};
  }

  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines = doc->get_line_count();
  m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                          visible_count);

  const std::size_t first_line = m_scrollbar.get_first_visible_line();
  const float clamped_y = std::clamp(
      py, layout.editor_bounds.y,
      std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
  const std::size_t clicked_row = static_cast<std::size_t>(std::max(
      static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
  const std::size_t line_index = visual_row_to_physical_line(
      m_folding, first_line + clicked_row, total_lines);

  const std::string_view line = doc->get_line(line_index);
  const float code_x =
      layout.editor_bounds.x + 14.0F * dpi - m_text_scroll_offset;
  const float target_x = std::max(px - code_x, 0.0F);

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

} // namespace Zenvra::Platform::Cocoa::Components