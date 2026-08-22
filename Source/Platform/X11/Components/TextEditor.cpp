#include "Platform/X11/Components/TextEditor.h"

#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Language/Protocol/LspProtocolSerializer.h"
#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Zenvra::Platform::X11::Components {

namespace {

using Zenvra::Utility::round_to_int;

constexpr float FIXED_MINIMAP_WIDTH = 112.0F;
constexpr float FIXED_SCROLLBAR_WIDTH = 14.0F;
constexpr float MIN_PANE_WIDTH_FOR_MINIMAP = 120.0F;

std::string make_lsp_uri(std::string_view filename) {
  if (filename.empty() || filename.starts_with("Untitled") ||
      filename.starts_with("untitled")) {
    return "file:///untitled.cpp";
  }
  return Language::Protocol::LspProtocolSerializer::path_to_uri(
      std::filesystem::path(filename));
}

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
  std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
  const std::string active_uri = get_active_document_uri();
  const std::string active_fname = get_active_document_filename();
  if (uri == active_uri || uri.ends_with(active_fname) ||
      (uri.starts_with("file://") && active_uri.ends_with(uri.substr(7)))) {
    if (auto *doc = m_controller.get_active_document()) {
      doc->set_diagnostics(diags);
    }
  }
  if (m_is_split && m_split_document_index.has_value()) {
    if (auto *split_doc = m_controller.get_document(*m_split_document_index)) {
      const std::string split_fname = std::string(split_doc->get_file_name());
      if (uri.ends_with(split_fname)) {
        split_doc->set_diagnostics(diags);
      }
    }
  }
}

bool TextEditor::open_file(const std::filesystem::path &path) {
  const bool opened = m_controller.open_file(path);
  if (opened) {
    m_scrollbar.reset();
    m_tab_scroll_offset = 0.0F;
    m_tab_animated_offset_x.clear();
    m_focused = true;
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_last_brace_caret = {};
    m_brace_animation.clear();
    m_completion_popup.hide();
    m_hover_tooltip.hide();
    m_signature_help.hide();
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
      m_folding.rebuild(doc->get_lines(), 4);

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

bool TextEditor::open_file_at_location(const std::filesystem::path &path,
                                       std::size_t line, std::size_t column) {
  const bool opened = open_file(path);
  if (auto *doc = get_focused_document()) {
    doc->set_caret(line, column, false);
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    return true;
  }
  return opened;
}

bool TextEditor::go_to_definition() {
  auto *doc = get_focused_document();
  if (doc == nullptr) {
    return false;
  }

  const std::string uri = get_active_document_uri();
  const std::string fname = get_active_document_filename();
  const Language::Protocol::Position pos{
      .line = doc->get_caret_line(),
      .character = doc->get_caret_column(),
  };

  Language::LanguageServerManager::instance().request_definition(
      uri, fname, pos,
      [this](std::vector<Language::Protocol::Location> locations) {
        if (locations.empty()) {
          return;
        }
        const auto &loc = locations.front();
        std::filesystem::path target_path =
            Language::Protocol::LspProtocolSerializer::uri_to_path(loc.uri);
        if (target_path.empty()) {
          return;
        }
        static_cast<void>(open_file_at_location(
            target_path, loc.range.start.line, loc.range.start.character));
      });

  return true;
}

bool TextEditor::close_file(const std::filesystem::path &path) {
  if (m_is_split && m_split_document_index.has_value()) {
    const auto docs = m_controller.get_documents();
    if (*m_split_document_index < docs.size() &&
        docs[*m_split_document_index].path == path) {
      m_is_split = false;
      m_split_document_index.reset();
      m_focused_pane = SplitPaneFocus::Left;
    }
  }

  const auto docs = m_controller.get_documents();
  for (std::size_t i = 0; i < docs.size(); ++i) {
    if (docs[i].path == path) {
      const bool closed = m_controller.close_file(i);
      m_hovered_tab_index.reset();
      m_hovered_tab_close_index.reset();
      m_completion_popup.hide();
      m_hover_tooltip.hide();
      m_signature_help.hide();
      return closed;
    }
  }
  return false;
}

bool TextEditor::close_all_files() {
  m_split_document_index.reset();
  m_is_split = false;
  m_is_resizing_split = false;
  m_focused_pane = SplitPaneFocus::Left;
  m_completion_popup.hide();
  m_hover_tooltip.hide();
  m_signature_help.hide();
  m_tab_scroll_offset = 0.0F;
  m_text_scroll_offset = 0.0F;
  m_scrollbar.reset();
  m_split_scrollbar.reset();
  m_hovered_tab_index.reset();
  m_hovered_tab_close_index.reset();
  m_hovered_fold_line.reset();
  return m_controller.close_all_files();
}

void TextEditor::reset_split() noexcept {
  m_is_split = false;
  m_split_document_index.reset();
  m_focused_pane = SplitPaneFocus::Left;
  m_scrollbar.reset();
  m_split_scrollbar.reset();
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
      m_folding.rebuild(doc->get_lines(), 4);

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
      m_folding.rebuild(doc->get_lines(), 4);

      Language::LanguageServerManager::instance().on_document_opened(
          uri, fname, 1, content);
    }
  }
  return created;
}

void TextEditor::close_all_documents() {
  while (!m_controller.get_documents().empty()) {
    static_cast<void>(m_controller.close_file(0));
  }
  m_scrollbar.reset();
  m_reveal_caret_pending = true;
  m_caret_blink.reset();
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
  m_caret_blink.reset();
  m_hovered_tab_index.reset();
  m_hovered_tab_close_index.reset();
}

void TextEditor::show_tab_action_menu(
    const UI::Editor::StudioEditorLayoutResult &layout) {
  m_tab_action_menu.visible = true;
  m_tab_action_menu.items = {
      {"Close All", "Ctrl+K Ctrl+W", false, false, false},
      {"Close Saved", "", false, false, false},
      {"", "", true, false, false},
      {"Keep Open", "", false, !m_preview_editors_enabled, true},
      {"", "", true, false, false},
      {"Split Right", "Ctrl+\\", false, false, false},
  };

  const float scale = layout.dpi_scale;
  const float menu_w = 210.0F * scale;
  const float item_h = 26.0F * scale;
  const float separator_h = 7.0F * scale;
  const float vertical_padding = 5.0F * scale;

  float total_items_h = 0.0F;
  for (const auto &it : m_tab_action_menu.items) {
    total_items_h += it.is_separator ? separator_h : item_h;
  }
  const float menu_h = total_items_h + vertical_padding * 2.0F;

  // Align directly to the right edge of the ellipsis button
  const float btn_right =
      (!m_tab_action_bounds[3].is_empty())
          ? m_tab_action_bounds[3].right()
          : (layout.editor_header_bounds.right() - 4.0F * scale);
  const float btn_bottom = (!m_tab_action_bounds[3].is_empty())
                               ? m_tab_action_bounds[3].bottom()
                               : (layout.editor_header_bounds.bottom());

  const float menu_x = std::max(btn_right - menu_w, 8.0F * scale);
  const float menu_y = btn_bottom + 4.0F * scale;

  m_tab_action_menu.bounds = UI::Rect{menu_x, menu_y, menu_w, menu_h};
  m_tab_action_menu.item_bounds.clear();
  float curr_y = menu_y + vertical_padding;
  for (const auto &it : m_tab_action_menu.items) {
    const float h = it.is_separator ? separator_h : item_h;
    m_tab_action_menu.item_bounds.push_back(
        UI::Rect{menu_x, curr_y, menu_w, h});
    curr_y += h;
  }
}

void TextEditor::draw_editor_header(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const float scale = surface.m_dpi_scale;
  const UI::Rect header_bounds = layout.editor_header_bounds;
  if (header_bounds.is_empty() || header_bounds.height <= 2.0F) {
    return;
  }

  // Background for editor header
  surface.fill_rectangle(drawable, header_bounds,
                         surface.m_pixels.editor_background);

  const float btn_w = 22.0F * scale;
  const float btn_h = 22.0F * scale;
  const float center_y = header_bounds.y + header_bounds.height * 0.5F;
  const float btn_y = center_y - btn_h * 0.5F;
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  const auto *document = m_controller.get_active_document();

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const UI::Rect left_header{header_bounds.x, header_bounds.y,
                               splitter_x - header_bounds.x,
                               header_bounds.height};
    const UI::Rect right_header{splitter_x + 2.0F * scale, header_bounds.y,
                                header_bounds.right() -
                                    (splitter_x + 2.0F * scale),
                                header_bounds.height};

    // Left Header File Title with Icon
    if (document != nullptr) {
      const std::string left_filename{document->get_file_name()};
      const std::string left_icon = UI::Editor::file_icon_asset_for_path(
          std::filesystem::path{left_filename});
      const int left_icon_sz = std::max(round_to_int(13.0F * scale), 11);
      const float left_icon_cx =
          left_header.x + 12.0F * scale + left_icon_sz * 0.5F;
      surface.draw_svg_icon(drawable, "Assets/icons/" + left_icon,
                            round_to_int(left_icon_cx), round_to_int(center_y),
                            left_icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background);

      if (surface.m_small_font) {
        surface.draw_text(drawable, *surface.m_small_font, left_filename,
                          left_header.x + 12.0F * scale + left_icon_sz +
                              6.0F * scale,
                          center_y, surface.m_text.primary);
      }
    }

    // Split close button on far right of right header
    m_split_close_btn_bounds = UI::Rect{
        right_header.right() - btn_w - 4.0F * scale, btn_y, btn_w, btn_h};
    if (m_hovered_split_close) {
      surface.fill_rounded_rectangle(
          drawable, m_split_close_btn_bounds, surface.m_pixels.hover_background,
          3.0F * scale, surface.m_pixels.editor_background);
    }
    surface.draw_svg_icon(
        drawable, "Assets/icons/diagnostic-error.svg",
        round_to_int(m_split_close_btn_bounds.x + btn_w * 0.5F),
        round_to_int(center_y), std::max(round_to_int(12.0F * scale), 10),
        m_hovered_split_close ? surface.m_palette.text_primary
                              : surface.m_palette.text_muted,
        surface.m_palette.editor_background);

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
      surface.draw_svg_icon(drawable, "Assets/icons/" + right_icon,
                            round_to_int(right_icon_cx), round_to_int(center_y),
                            right_icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background);

      if (surface.m_small_font) {
        surface.draw_text(drawable, *surface.m_small_font, right_filename,
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

    // Left side: File title with Icon (Clipped before buttons)
    if (document != nullptr) {
      const std::string filename{document->get_file_name()};
      const std::string icon_asset =
          UI::Editor::file_icon_asset_for_path(std::filesystem::path{filename});
      const int icon_sz = std::max(round_to_int(13.0F * scale), 11);
      const float icon_cx = header_bounds.x + 12.0F * scale + icon_sz * 0.5F;
      surface.draw_svg_icon(drawable, "Assets/icons/" + icon_asset,
                            round_to_int(icon_cx), round_to_int(center_y),
                            icon_sz, surface.m_palette.text_muted,
                            surface.m_palette.editor_background);

      if (surface.m_small_font) {
        surface.draw_text(drawable, *surface.m_small_font, filename,
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

    const auto &ed_bg = surface.m_palette.editor_background;
    const std::uint32_t a = is_active_menu ? 45 : 25;
    const UI::Theme::Color blended_btn_bg{
        static_cast<std::uint8_t>((255 * a + ed_bg.red * (255 - a) + 127) /
                                  255),
        static_cast<std::uint8_t>((255 * a + ed_bg.green * (255 - a) + 127) /
                                  255),
        static_cast<std::uint8_t>((255 * a + ed_bg.blue * (255 - a) + 127) /
                                  255),
        255};

    if (is_active_menu || is_hovered) {
      surface.fill_rounded_rectangle(
          drawable, btn, surface.allocate_color(blended_btn_bg), 3.0F * scale,
          surface.m_pixels.editor_background);
    }

    surface.draw_svg_icon(
        drawable, icons[i], round_to_int(btn.x + btn.width * 0.5F),
        round_to_int(center_y), icon_sizes[i],
        (is_active_menu || is_hovered) ? surface.m_palette.text_primary
                                       : surface.m_palette.text_muted,
        (is_active_menu || is_hovered) ? blended_btn_bg
                                       : surface.m_palette.editor_background);
  }

  // Draw header bottom border
  const int header_bottom = round_to_int(header_bounds.bottom()) - 1;
  surface.draw_line(drawable, round_to_int(header_bounds.x), header_bottom,
                    round_to_int(header_bounds.right()), header_bottom,
                    surface.m_pixels.border);
}

void TextEditor::draw_tab_action_menu(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  static_cast<void>(layout);
  if (!m_tab_action_menu.visible) {
    return;
  }

  const float scale = surface.m_dpi_scale;
  const auto &menu = m_tab_action_menu;
  const int radius = std::max(round_to_int(6.0F * scale), 5);
  AntialiasedFont &font = (surface.m_ui_font != nullptr)
                              ? *surface.m_ui_font
                              : *surface.m_small_font;

  // Acrylic ambient outer shadow
  const UI::Rect outer_shadow{
      menu.bounds.x - 2.0F * scale, menu.bounds.y - 1.0F * scale,
      menu.bounds.width + 4.0F * scale, menu.bounds.height + 4.0F * scale};
  surface.fill_rounded_rectangle(
      drawable, outer_shadow,
      surface.allocate_color(UI::Theme::Color{12, 13, 16, 255}), radius + 2,
      surface.m_pixels.editor_background);

  // Mid shadow
  const UI::Rect mid_shadow{menu.bounds.x - 1.0F * scale, menu.bounds.y,
                            menu.bounds.width + 2.0F * scale,
                            menu.bounds.height + 2.0F * scale};
  surface.fill_rounded_rectangle(
      drawable, mid_shadow,
      surface.allocate_color(UI::Theme::Color{18, 19, 23, 255}), radius + 1,
      surface.m_pixels.editor_background);

  // Card Background matching Menubar / Studio Theme
  const UI::Theme::Color popup_bg{30, 31, 38, 255};
  surface.fill_rounded_rectangle(drawable, menu.bounds,
                                 surface.allocate_color(popup_bg), radius,
                                 surface.m_pixels.editor_background);

  // Translucent Hairline Rounded Border
  surface.draw_rounded_rectangle(
      drawable, menu.bounds,
      surface.allocate_color(UI::Theme::Color{70, 72, 80, 255}),
      static_cast<float>(radius));

  for (std::size_t i = 0; i < menu.items.size() && i < menu.item_bounds.size();
       ++i) {
    const auto &item = menu.items[i];
    const auto &rect = menu.item_bounds[i];

    if (item.is_separator) {
      const float sep_y = rect.y + rect.height * 0.5F;
      surface.fill_rectangle(
          drawable,
          UI::Rect{rect.x + 10.0F * scale, sep_y, rect.width - 20.0F * scale,
                   1.0F * scale},
          surface.allocate_color(UI::Theme::Color{55, 58, 70, 255}));
      continue;
    }

    const bool hovered = (menu.hovered_index && *menu.hovered_index == i);
    if (hovered) {
      UI::Rect hover_bounds = rect;
      hover_bounds.x += 5.0F * scale;
      hover_bounds.width -= 10.0F * scale;
      hover_bounds.y += 1.0F * scale;
      hover_bounds.height -= 2.0F * scale;
      surface.fill_rounded_rectangle(
          drawable, hover_bounds,
          surface.allocate_color(UI::Theme::Color{53, 132, 228, 240}),
          std::max(round_to_int(4.0F * scale), 3),
          surface.allocate_color(popup_bg));
    }

    const float center_y = rect.y + rect.height * 0.5F;
    float text_x = rect.x + 12.0F * scale;

    if (item.has_checkbox && item.is_checked) {
      surface.draw_svg_icon(drawable, "Assets/icons/check.svg",
                            round_to_int(rect.x + 10.0F * scale),
                            round_to_int(center_y),
                            std::max(round_to_int(12.0F * scale), 10),
                            hovered ? UI::Theme::Color{255, 255, 255, 255}
                                    : surface.m_palette.accent,
                            popup_bg);
      text_x += 14.0F * scale;
    }

    const std::string label_color = hovered ? "#FFFFFF" : "#CCCCCC";
    surface.draw_text(drawable, font, item.label, text_x, center_y,
                      label_color);

    if (!item.shortcut.empty()) {
      const int sc_w = font.getTextWidth(item.shortcut);
      const std::string shortcut_color = hovered ? "#FFFFFF" : "#8A8A8A";
      surface.draw_text(drawable, font, item.shortcut,
                        rect.right() - static_cast<float>(sc_w) - 14.0F * scale,
                        center_y, shortcut_color);
    }
  }
}

void TextEditor::draw_split_drop_overlay(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  if (m_active_drop_zone == SplitDropZone::NoneZone ||
      !m_tab_drag_drop.is_dragging()) {
    return;
  }

  const float scale = surface.m_dpi_scale;
  const UI::Rect ed = layout.editor_bounds;
  UI::Rect drop_rect = ed;

  switch (m_active_drop_zone) {
  case SplitDropZone::Left:
    drop_rect = UI::Rect{ed.x, ed.y, ed.width * 0.5F, ed.height};
    break;
  case SplitDropZone::Right:
    drop_rect =
        UI::Rect{ed.x + ed.width * 0.5F, ed.y, ed.width * 0.5F, ed.height};
    break;
  case SplitDropZone::Top:
    drop_rect = UI::Rect{ed.x, ed.y, ed.width, ed.height * 0.5F};
    break;
  case SplitDropZone::Bottom:
    drop_rect =
        UI::Rect{ed.x, ed.y + ed.height * 0.5F, ed.width, ed.height * 0.5F};
    break;
  case SplitDropZone::Center:
  case SplitDropZone::NoneZone:
    return;
  }

  surface.fill_rounded_rectangle(
      drawable, drop_rect, surface.m_pixels.tab_active_background, 4.0F * scale,
      surface.m_pixels.editor_background);
  surface.draw_rectangle(drawable, drop_rect, surface.m_pixels.accent);
}

bool TextEditor::is_split_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  if (!m_is_split || !m_split_document_index.has_value() ||
      *m_split_document_index >= m_controller.get_documents().size()) {
    return false;
  }
  const float scale = layout.dpi_scale;
  const float split_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
  const float handle_w = 6.0F * scale;
  const float top_y = (!layout.editor_header_bounds.is_empty() &&
                       layout.editor_header_bounds.height > 2.0F)
                          ? layout.editor_header_bounds.y
                          : layout.editor_bounds.y;
  const float total_h = (!layout.editor_header_bounds.is_empty() &&
                         layout.editor_header_bounds.height > 2.0F)
                            ? (layout.editor_header_bounds.height +
                               layout.editor_bounds.height)
                            : layout.editor_bounds.height;
  const UI::Rect handle_rect{split_x - handle_w * 0.5F, top_y, handle_w,
                             total_h};
  return handle_rect.contains(point_x, point_y);
}

bool TextEditor::is_fold_margin_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float scale = surface.m_dpi_scale;
  const float fold_margin = 12.0F * scale;
  const float left_fold_margin_left =
      layout.gutter_bounds.right() - fold_margin;
  if (layout.gutter_bounds.contains(point_x, point_y) &&
      point_x >= left_fold_margin_left) {
    return true;
  }
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float right_gutter_w = 48.0F * scale;
    const float right_gutter_x = splitter_x + 2.0F * scale;
    const float right_fold_margin_left =
        right_gutter_x + right_gutter_w - fold_margin;
    const UI::Rect right_gutter{right_gutter_x, layout.editor_bounds.y,
                                right_gutter_w, layout.editor_bounds.height};
    if (right_gutter.contains(point_x, point_y) &&
        point_x >= right_fold_margin_left) {
      return true;
    }
  }
  return false;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  static_cast<void>(surface);
  if (m_tab_action_menu.visible &&
      m_tab_action_menu.bounds.contains(point_x, point_y)) {
    return true;
  }
  for (std::size_t i = 0; i < 4; ++i) {
    if (m_tab_action_bounds[i].contains(point_x, point_y)) {
      return true;
    }
  }
  if (layout.editor_header_bounds.contains(point_x, point_y)) {
    return true;
  }
  if (!layout.tab_bar_bounds.contains(point_x, point_y)) {
    return false;
  }
  for (std::size_t i = 0; i < m_tab_count; ++i) {
    if (m_tab_bounds[i].contains(point_x, point_y)) {
      return true;
    }
  }
  return false;
}

bool TextEditor::is_empty_state_interactive_point(
    float point_x, float point_y) const noexcept {
  if (m_controller.get_active_document() != nullptr)
    return false;
  return m_empty_state_open_btn.get_bounds().contains(point_x, point_y) ||
         m_empty_state_clone_btn.get_bounds().contains(point_x, point_y);
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, bool extend_selection, int click_count,
    std::string &command_out) {
  m_focused = true;
  {
    std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
    m_completion_popup.hide();
    m_signature_help.hide();
  }

  // Handle Tab Action Menu selection
  if (m_tab_action_menu.visible) {
    if (m_tab_action_menu.bounds.contains(point_x, point_y)) {
      for (std::size_t i = 0; i < m_tab_action_menu.item_bounds.size(); ++i) {
        if (m_tab_action_menu.item_bounds[i].contains(point_x, point_y)) {
          const auto &item = m_tab_action_menu.items[i];
          m_tab_action_menu.visible = false;
          if (item.label == "Close All") {
            close_all_documents();
          } else if (item.label == "Close Saved") {
            close_saved_documents();
          } else if (item.label == "Keep Open") {
            m_preview_editors_enabled = !m_preview_editors_enabled;
          } else if (item.label == "Split Right") {
            command_out = "zde.editor.split_right";
            static_cast<void>(handle_command("zde.editor.split_right"));
          }
          return true;
        }
      }
    }
    m_tab_action_menu.visible = false;
    return true;
  }

  // Handle Tab Action Toolbar buttons (0: Split Right, 1: Prev Tab, 2: Next
  // Tab, 3: More)
  for (std::size_t i = 0; i < 4; ++i) {
    if (m_tab_action_bounds[i].contains(point_x, point_y)) {
      if (i == 0) {
        m_is_split = !m_is_split;
        if (m_is_split) {
          const auto doc_count = m_controller.get_documents().size();
          if (doc_count > 1) {
            const std::size_t active =
                m_controller.get_active_index().value_or(0);
            m_split_document_index = (active + 1) % doc_count;
          } else {
            m_split_document_index =
                m_controller.get_active_index().value_or(0);
          }
          m_focused_pane = SplitPaneFocus::Right;
        } else {
          m_split_document_index.reset();
          m_focused_pane = SplitPaneFocus::Left;
        }
      } else if (i == 1) {
        const auto doc_count = m_controller.get_documents().size();
        if (doc_count > 1) {
          std::size_t active = m_controller.get_active_index().value_or(0);
          std::size_t prev = (active == 0) ? (doc_count - 1) : (active - 1);
          static_cast<void>(m_controller.activate_file(prev));
          m_scrollbar.reset();
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
        }
      } else if (i == 2) {
        const auto doc_count = m_controller.get_documents().size();
        if (doc_count > 1) {
          std::size_t active = m_controller.get_active_index().value_or(0);
          std::size_t next = (active + 1) % doc_count;
          static_cast<void>(m_controller.activate_file(next));
          m_scrollbar.reset();
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
        }
      } else if (i == 3) {
        show_tab_action_menu(layout);
      }
      return true;
    }
  }

  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float scale = surface.m_dpi_scale;
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  if (is_split_active) {
    const float scroll_top_y = layout.editor_bounds.y;
    const float scroll_total_h = layout.editor_bounds.height;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y,
                               splitter_x - layout.editor_bounds.x,
                               scroll_total_h};
    const float left_minimap_w =
        (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w,
                                  scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y,
                                left_minimap_w, scroll_total_h};

    const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y,
                                layout.editor_bounds.right() -
                                    (splitter_x + 2.0F * scale),
                                scroll_total_h};
    const float right_minimap_w =
        (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w,
                                   scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w,
                                 scroll_top_y, right_minimap_w, scroll_total_h};

    auto *left_doc = m_controller.get_active_document();
    auto *right_doc = m_controller.get_document(*m_split_document_index);
    const float line_height = 20.0F * scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));

    // Right Close Button
    if (m_split_close_btn_bounds.contains(point_x, point_y)) {
      m_is_split = false;
      m_split_document_index.reset();
      m_focused_pane = SplitPaneFocus::Left;
      return true;
    }

    // Right Minimap
    if (right_doc != nullptr && right_minimap.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Right;
      UI::Editor::StudioEditorLayoutResult rlay = layout;
      rlay.minimap_bounds = right_minimap;
      rlay.scrollbar_bounds = right_scrollbar;
      m_split_scrollbar.synchronize(
          count_visible_lines(m_split_folding, right_doc->get_line_count()),
          visible_count);
      const auto target = m_split_minimap.handle_pointer_press(
          rlay, point_x, point_y, right_doc->get_line_count(), visible_count,
          m_split_scrollbar.get_first_visible_line());
      if (target)
        static_cast<void>(m_split_scrollbar.scroll_to(*target));
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return true;
    }

    // Right Scrollbar
    if (right_doc != nullptr && right_scrollbar.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Right;
      UI::Editor::StudioEditorLayoutResult rlay = layout;
      rlay.minimap_bounds = right_minimap;
      rlay.scrollbar_bounds = right_scrollbar;
      m_split_scrollbar.synchronize(
          count_visible_lines(m_split_folding, right_doc->get_line_count()),
          visible_count);
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return m_split_scrollbar.handle_pointer_press(rlay, point_x, point_y);
    }

    // Left Minimap
    if (left_doc != nullptr && left_minimap.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Left;
      UI::Editor::StudioEditorLayoutResult llay = layout;
      llay.minimap_bounds = left_minimap;
      llay.scrollbar_bounds = left_scrollbar;
      m_scrollbar.synchronize(
          count_visible_lines(m_folding, left_doc->get_line_count()),
          visible_count);
      const auto target = m_minimap.handle_pointer_press(
          llay, point_x, point_y, left_doc->get_line_count(), visible_count,
          m_scrollbar.get_first_visible_line());
      if (target)
        static_cast<void>(m_scrollbar.scroll_to(*target));
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return true;
    }

    // Left Scrollbar
    if (left_doc != nullptr && left_scrollbar.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Left;
      UI::Editor::StudioEditorLayoutResult llay = layout;
      llay.minimap_bounds = left_minimap;
      llay.scrollbar_bounds = left_scrollbar;
      m_scrollbar.synchronize(
          count_visible_lines(m_folding, left_doc->get_line_count()),
          visible_count);
      m_focused = true;
      m_pointer_selecting = false;
      m_reveal_caret_pending = false;
      m_caret_blink.reset();
      return m_scrollbar.handle_pointer_press(llay, point_x, point_y);
    }

    // Splitter Resize Handle
    if (is_split_resize_handle_point(layout, point_x, point_y)) {
      m_is_resizing_split = true;
      return true;
    }

    // Right Editor Pane (Code or Gutter)
    if (right_doc != nullptr && right_bounds.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Right;
      m_completion_popup.hide();
      m_signature_help.hide();
      m_focused = true;

      const float right_gutter_w = 48.0F * scale;
      const float fold_margin = 14.0F * scale;
      const float fold_margin_left =
          splitter_x + 2.0F * scale + right_gutter_w - fold_margin;
      if (point_x >= fold_margin_left &&
          point_x <= splitter_x + 2.0F * scale + right_gutter_w) {
        m_split_folding.rebuild(right_doc->get_lines(), 4);
        const std::size_t split_total_lines =
            std::max(right_doc->get_line_count(), std::size_t{1});
        const std::size_t clicked_row = static_cast<std::size_t>(std::max(
            static_cast<int>((point_y - layout.editor_bounds.y) / line_height),
            0));
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
          position_from_point(surface, layout, point_x, point_y);
      if (click_count == 2) {
        right_doc->select_word_at(pos.line, pos.column);
      } else if (click_count == 3) {
        right_doc->select_line_at(pos.line);
      } else {
        static_cast<void>(
            right_doc->set_caret(pos.line, pos.column, extend_selection));
      }
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
      return true;
    }

    // Left Editor Pane (Code or Gutter)
    if (left_doc != nullptr && left_bounds.contains(point_x, point_y)) {
      m_focused_pane = SplitPaneFocus::Left;
      m_completion_popup.hide();
      m_signature_help.hide();
      m_focused = true;

      const float fold_margin = 14.0F * scale;
      const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
      if (point_x >= fold_margin_left &&
          point_x <= layout.gutter_bounds.right()) {
        m_folding.rebuild(left_doc->get_lines(), 4);
        const std::size_t total_lines =
            std::max(left_doc->get_line_count(), std::size_t{1});
        const std::size_t clicked_row = static_cast<std::size_t>(std::max(
            static_cast<int>((point_y - layout.editor_bounds.y) / line_height),
            0));
        const std::size_t line_index = visual_row_to_physical_line(
            m_folding, m_scrollbar.get_first_visible_line() + clicked_row,
            total_lines);

        if (m_folding.is_fold_start(line_index)) {
          m_folding.toggle_fold(line_index);
          m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                                  visible_count);
          m_reveal_caret_pending = true;
          m_caret_blink.reset();
          return true;
        }
      }

      m_pointer_selecting = true;
      const UI::Editor::TextPosition pos =
          position_from_point(surface, layout, point_x, point_y);
      if (click_count == 2) {
        left_doc->select_word_at(pos.line, pos.column);
      } else if (click_count == 3) {
        left_doc->select_line_at(pos.line);
      } else {
        static_cast<void>(
            left_doc->set_caret(pos.line, pos.column, extend_selection));
      }
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
      return true;
    }
  }

  // Handle Empty State buttons
  if (m_controller.get_active_document() == nullptr) {
    if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y)) {
      command_out = Commands::CommandIds::file_open;
      return true;
    }
    if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
      command_out = "zde.git.clone";
      return true;
    }
    return false;
  }

  // Handle tab strip clicking
  if (layout.tab_bar_bounds.contains(point_x, point_y)) {
    for (std::size_t i = 0; i < m_tab_count; ++i) {
      if (m_tab_bounds[i].contains(point_x, point_y)) {
        const float close_btn_w = 16.0F * surface.m_dpi_scale;
        const float close_btn_x = m_tab_bounds[i].right() - close_btn_w - 4.0F;
        if (point_x >= close_btn_x) {
          const auto docs = m_controller.get_documents();
          if (i < docs.size()) {
            const float shift = m_tab_bounds[i].width + UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
            static_cast<void>(m_controller.close_file(i));
            const auto remaining = m_controller.get_documents();
            for (std::size_t k = i; k < remaining.size(); ++k) {
              m_tab_animated_offset_x[remaining[k].id] += shift;
            }
          }
          return true;
        }

        static_cast<void>(m_controller.activate_file(i));
        m_tab_drag_drop.begin_drag(i, point_x);
        m_drag_initial_tab_x = m_tab_bounds[i].x;
        m_focused_pane = SplitPaneFocus::Left;
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        return true;
      }
    }
    return false;
  }

  // Single view: Handle Folding Click
  if (layout.gutter_bounds.contains(point_x, point_y)) {
    const auto *doc = get_focused_document();
    if (doc) {
      m_folding.rebuild(doc->get_lines(), 4);
      const std::size_t total_lines =
          std::max(doc->get_line_count(), std::size_t{1});
      const float line_h = 20.0F * surface.m_dpi_scale;
      const std::size_t vis_lines = static_cast<std::size_t>(
          std::max(static_cast<int>(layout.editor_bounds.height / line_h), 1));
      const std::size_t first_line = m_scrollbar.get_first_visible_line();
      if (const auto fold_line = fold_start_line_at_point(
              m_folding, layout, point_x, point_y, surface.m_dpi_scale,
              first_line, total_lines)) {
        m_folding.toggle_fold(*fold_line);
        m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines),
                                vis_lines);
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        return true;
      }
    }
  }

  // Single view: Handle Minimap Click
  if (m_minimap.is_point(layout, point_x, point_y)) {
    m_reveal_caret_pending = false;
    const auto *doc = get_focused_document();
    if (doc) {
      const float line_h = 20.0F * surface.m_dpi_scale;
      const std::size_t vis_lines = static_cast<std::size_t>(
          std::max(static_cast<int>(layout.editor_bounds.height / line_h), 1));
      if (const auto target_line = m_minimap.handle_pointer_press(
              layout, point_x, point_y, doc->get_line_count(), vis_lines,
              m_scrollbar.get_first_visible_line())) {
        static_cast<void>(m_scrollbar.scroll_to(*target_line));
        return true;
      }
    }
    return true;
  }

  // Single view: Handle Scrollbar Click
  if (m_scrollbar.is_point(layout, point_x, point_y)) {
    m_reveal_caret_pending = false;
    const auto *doc = get_focused_document();
    if (doc) {
      const float line_h = 20.0F * surface.m_dpi_scale;
      const std::size_t vis_lines = static_cast<std::size_t>(
          std::max(static_cast<int>(layout.editor_bounds.height / line_h), 1));
      m_scrollbar.synchronize(
          count_visible_lines(m_folding, doc->get_line_count()), vis_lines);
      if (m_scrollbar.handle_pointer_press(layout, point_x, point_y)) {
        return true;
      }
    }
    return true;
  }

  // Single view: Handle Code Area Click -> Caret positioning & selection
  if (layout.editor_bounds.contains(point_x, point_y)) {
    const auto pos = position_from_point(surface, layout, point_x, point_y);
    auto *doc = get_focused_document();
    if (doc != nullptr) {
      if (click_count == 2) {
        doc->select_word_at(pos.line, pos.column);
      } else if (click_count == 3) {
        doc->select_line_at(pos.line);
      } else {
        doc->set_caret(pos.line, pos.column, extend_selection);
      }
      m_pointer_selecting = true;
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
    }
    return true;
  }

  return false;
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  bool changed = false;

  if (m_tab_action_menu.visible) {
    std::optional<std::size_t> next_hover;
    for (std::size_t i = 0; i < m_tab_action_menu.item_bounds.size(); ++i) {
      if (m_tab_action_menu.item_bounds[i].contains(point_x, point_y)) {
        next_hover = i;
        break;
      }
    }
    if (next_hover != m_tab_action_menu.hovered_index) {
      m_tab_action_menu.hovered_index = next_hover;
      changed = true;
    }
  }

  // Tab Action Toolbar buttons (0..3)
  std::optional<std::size_t> next_act_hover;
  for (std::size_t i = 0; i < 4; ++i) {
    if (m_tab_action_bounds[i].contains(point_x, point_y)) {
      next_act_hover = i;
      break;
    }
  }
  if (next_act_hover != m_hovered_tab_action) {
    m_hovered_tab_action = next_act_hover;
    changed = true;
  }

  // Split resize handle hover
  const bool next_split_resize_hover =
      is_split_resize_handle_point(layout, point_x, point_y);
  if (next_split_resize_hover != m_hovered_split_resize) {
    m_hovered_split_resize = next_split_resize_hover;
    changed = true;
  }

  // Split close button hover
  const bool next_split_close_hover =
      m_is_split && m_split_close_btn_bounds.contains(point_x, point_y);
  if (next_split_close_hover != m_hovered_split_close) {
    m_hovered_split_close = next_split_close_hover;
    changed = true;
  }

  // Tab strip hover
  std::optional<std::size_t> next_tab_hover;
  std::optional<std::size_t> next_close_hover;
  if (layout.tab_bar_bounds.contains(point_x, point_y)) {
    for (std::size_t i = 0; i < m_tab_count; ++i) {
      if (m_tab_bounds[i].contains(point_x, point_y)) {
        next_tab_hover = i;
        const float close_btn_w = 16.0F * layout.dpi_scale;
        const float close_btn_x = m_tab_bounds[i].right() - close_btn_w - 4.0F;
        if (point_x >= close_btn_x) {
          next_close_hover = i;
        }
        break;
      }
    }
  }
  if (next_tab_hover != m_hovered_tab_index ||
      next_close_hover != m_hovered_tab_close_index) {
    m_hovered_tab_index = next_tab_hover;
    m_hovered_tab_close_index = next_close_hover;
    changed = true;
  }

  // Empty state buttons hover
  if (m_controller.get_active_document() == nullptr) {
    changed |= m_empty_state_open_btn.handle_pointer_move(point_x, point_y);
    changed |= m_empty_state_clone_btn.handle_pointer_move(point_x, point_y);
  }

  // LSP Diagnostic Hover detection
  if (layout.editor_bounds.contains(point_x, point_y)) {
    const auto *doc = get_focused_document();
    if (doc != nullptr) {
      const float line_h = 20.0F * layout.dpi_scale;
      const std::size_t first_line = m_scrollbar.get_first_visible_line();
      const std::size_t clicked_row = static_cast<std::size_t>(std::max(
          static_cast<int>((point_y - layout.editor_bounds.y) / line_h), 0));
      const std::size_t line_index = visual_row_to_physical_line(
          m_folding, first_line + clicked_row, doc->get_line_count());

      std::optional<HoveredDiagnosticInfo> found_diag;
      for (const auto &diag : doc->get_diagnostics()) {
        if (static_cast<std::size_t>(diag.range.start.line) == line_index) {
          found_diag = HoveredDiagnosticInfo{
              .diagnostic = diag,
              .line_text = std::string(doc->get_line(line_index)),
              .symbol_name = "",
              .anchor_x = point_x,
              .anchor_y = point_y,
          };
          break;
        }
      }
      if (found_diag.has_value()) {
        m_hover_tooltip.hide();
        if (found_diag.has_value() != m_hovered_diagnostic.has_value()) {
          m_hovered_diagnostic = found_diag;
          changed = true;
        }
      } else {
        if (m_hovered_diagnostic.has_value()) {
          m_hovered_diagnostic.reset();
          changed = true;
        }
        // LSP symbol hover
        const float char_w = 8.5F * layout.dpi_scale;
        const float code_start_x =
            layout.gutter_bounds.right() + 8.0F * layout.dpi_scale;
        if (point_x >= code_start_x && line_index < doc->get_line_count()) {
          const std::size_t hover_col = static_cast<std::size_t>(
              std::max(0, static_cast<int>((point_x - code_start_x) / char_w)));
          const std::string uri = get_active_document_uri();
          const std::string fname = get_active_document_filename();
          const Language::Protocol::Position pos{
              .line = line_index,
              .character = hover_col,
          };
          Language::LanguageServerManager::instance().request_hover(
              uri, fname, pos, std::string(doc->get_line(line_index)),
              [this, point_x,
               point_y](std::optional<Language::Protocol::Hover> hover) {
                std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
                if (hover.has_value() && !hover->contents.empty()) {
                  m_hover_tooltip.show(hover->contents, point_x + 10.0F,
                                       point_y + 16.0F);
                } else {
                  m_hover_tooltip.hide();
                }
                m_lsp_dirty = true;
              });
        } else {
          m_hover_tooltip.hide();
        }
      }
    }
  } else {
    if (m_hovered_diagnostic.has_value()) {
      m_hovered_diagnostic.reset();
      changed = true;
    }
    m_hover_tooltip.hide();
  }

  return changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  // Split pane divider drag resize
  if (m_is_resizing_split) {
    const float ed_w = layout.editor_bounds.width;
    const float rel_x = point_x - layout.editor_bounds.x;
    m_split_ratio = std::clamp(rel_x / ed_w, 0.15F, 0.85F);
    return true;
  }

  // Tab drag & drop
  if (m_tab_drag_drop.is_dragging()) {
    m_tab_drag_drop.drag(point_x);
    m_drag_cursor_x = point_x;
    m_drag_cursor_y = point_y;

    if (point_y > layout.tab_bar_bounds.bottom()) {
      if (layout.editor_bounds.contains(point_x, point_y)) {
        const float edge_thresh = 40.0F * surface.m_dpi_scale;
        const auto &ed = layout.editor_bounds;
        if (point_x - ed.x < edge_thresh)
          m_active_drop_zone = SplitDropZone::Left;
        else if (ed.right() - point_x < edge_thresh)
          m_active_drop_zone = SplitDropZone::Right;
        else if (point_y - ed.y < edge_thresh)
          m_active_drop_zone = SplitDropZone::Top;
        else if (ed.bottom() - point_y < edge_thresh)
          m_active_drop_zone = SplitDropZone::Bottom;
        else
          m_active_drop_zone = SplitDropZone::Center;
      } else {
        m_active_drop_zone = SplitDropZone::NoneZone;
      }
    } else {
      m_active_drop_zone = SplitDropZone::NoneZone;
      float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
      const auto documents = m_controller.get_documents();
      for (std::size_t index = 0; index < documents.size(); ++index) {
        const float text_w = static_cast<float>(surface.m_ui_font->getTextWidth(
            documents[index].text.get_file_name()));
        const float width =
            UI::Editor::calculate_editor_tab_width(text_w, surface.m_dpi_scale);
        const UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width,
                              layout.tab_bar_bounds.height};
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
    }
    return true;
  }

  const float scale = surface.m_dpi_scale;
  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();

  if (is_split_active) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scroll_top_y = layout.editor_bounds.y;
    const float scroll_total_h = layout.editor_bounds.height;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y,
                               splitter_x - layout.editor_bounds.x,
                               scroll_total_h};
    const float left_minimap_w =
        (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w,
                                  scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y,
                                left_minimap_w, scroll_total_h};

    const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y,
                                layout.editor_bounds.right() -
                                    (splitter_x + 2.0F * scale),
                                scroll_total_h};
    const float right_minimap_w =
        (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w,
                                   scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w,
                                 scroll_top_y, right_minimap_w, scroll_total_h};

    const float line_height = 20.0F * scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));

    if (const auto *left_doc = m_controller.get_active_document()) {
      UI::Editor::StudioEditorLayoutResult llay = layout;
      llay.minimap_bounds = left_minimap;
      llay.scrollbar_bounds = left_scrollbar;
      const auto target = m_minimap.handle_pointer_drag(
          llay, point_y, left_doc->get_line_count(), visible_count,
          m_scrollbar.get_first_visible_line());
      if (target) {
        static_cast<void>(m_scrollbar.scroll_to(*target));
        m_reveal_caret_pending = false;
        return true;
      }
      if (m_scrollbar.handle_pointer_drag(llay, point_y)) {
        m_reveal_caret_pending = false;
        return true;
      }
    }

    if (const auto *right_doc =
            m_controller.get_document(*m_split_document_index)) {
      UI::Editor::StudioEditorLayoutResult rlay = layout;
      rlay.minimap_bounds = right_minimap;
      rlay.scrollbar_bounds = right_scrollbar;
      const auto target = m_split_minimap.handle_pointer_drag(
          rlay, point_y, right_doc->get_line_count(), visible_count,
          m_split_scrollbar.get_first_visible_line());
      if (target) {
        static_cast<void>(m_split_scrollbar.scroll_to(*target));
        m_reveal_caret_pending = false;
        return true;
      }
      if (m_split_scrollbar.handle_pointer_drag(rlay, point_y)) {
        m_reveal_caret_pending = false;
        return true;
      }
    }
  } else {
    if (const auto *document = m_controller.get_active_document()) {
      const float line_height = 20.0F * scale;
      const std::size_t visible_count = static_cast<std::size_t>(std::max(
          static_cast<int>(layout.editor_bounds.height / line_height), 1));
      const auto target = m_minimap.handle_pointer_drag(
          layout, point_y, document->get_line_count(), visible_count,
          m_scrollbar.get_first_visible_line());
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
  }

  // Pointer selecting text
  if (m_pointer_selecting) {
    m_is_drag_selecting =
        true; // Now we know it's an actual drag, not just a click
    auto &scrollbar =
        (is_split_active && m_focused_pane == SplitPaneFocus::Right)
            ? m_split_scrollbar
            : m_scrollbar;
    const float top_boundary = layout.editor_bounds.y;
    const float bottom_boundary = layout.editor_bounds.bottom();

    if (point_y < top_boundary) {
      const float diff = top_boundary - point_y;
      const std::ptrdiff_t step = diff > 40.0F * scale ? -3 : -1;
      static_cast<void>(scrollbar.scroll_lines(step));
    } else if (point_y > bottom_boundary) {
      const float diff = point_y - bottom_boundary;
      const std::ptrdiff_t step = diff > 40.0F * scale ? 3 : 1;
      static_cast<void>(scrollbar.scroll_lines(step));
    }

    const auto pos = position_from_point(surface, layout, point_x, point_y);
    auto *doc = get_focused_document();
    if (doc != nullptr) {
      doc->set_caret(pos.line, pos.column, true);
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
    }
    return true;
  }

  return false;
}

bool TextEditor::handle_pointer_release() noexcept {
  const bool was_resizing = m_is_resizing_split;
  const bool was_selecting = m_pointer_selecting;
  const bool minimap_released = m_minimap.handle_pointer_release();
  const bool split_minimap_released = m_split_minimap.handle_pointer_release();
  const bool sb_released = m_scrollbar.handle_pointer_release();
  const bool split_sb_released = m_split_scrollbar.handle_pointer_release();
  m_is_resizing_split = false;
  m_pointer_selecting = false;
  m_is_drag_selecting = false;

  if (m_tab_drag_drop.is_dragging()) {
    const auto drag_idx = m_tab_drag_drop.get_dragged_index();
    if (m_active_drop_zone != SplitDropZone::NoneZone) {
      if (m_active_drop_zone == SplitDropZone::Right ||
          m_active_drop_zone == SplitDropZone::Left) {
        m_is_split = true;
        m_split_document_index = drag_idx;
        m_focused_pane = SplitPaneFocus::Right;
      }
    }
    m_tab_drag_drop.end_drag();
    m_active_drop_zone = SplitDropZone::NoneZone;
    return true;
  }

  return was_resizing || was_selecting || minimap_released ||
         split_minimap_released || sb_released || split_sb_released;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y, std::string &command_out, std::ptrdiff_t line_delta,
    bool horizontal) noexcept {
  static_cast<void>(command_out);
  {
    std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
    if (m_completion_popup.is_visible() &&
        m_completion_popup.is_point_inside(point_x, point_y,
                                           24.0F * surface.m_dpi_scale,
                                           340.0F * surface.m_dpi_scale)) {
      return m_completion_popup.scroll(line_delta);
    }
  }

  if (layout.tab_bar_bounds.contains(point_x, point_y)) {
    m_tab_scroll_offset =
        std::clamp(m_tab_scroll_offset + static_cast<float>(line_delta) * 20.0F,
                   0.0F, std::max(0.0F, m_max_tab_scroll));
    return true;
  }

  const float line_h = 20.0F * surface.m_dpi_scale;
  const std::size_t vis_lines = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_h), 1));

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float split_x =
        layout.editor_bounds.x + layout.editor_bounds.width * m_split_ratio;
    if (point_x >= split_x) {
      const auto *split_doc =
          m_controller.get_document(*m_split_document_index);
      if (split_doc) {
        m_split_scrollbar.synchronize(
            count_visible_lines(m_split_folding, split_doc->get_line_count()),
            vis_lines);
        m_reveal_caret_pending = false;
        return m_split_scrollbar.scroll_lines(line_delta);
      }
    }
  }

  const auto *doc = m_controller.get_active_document();
  if (doc) {
    if (horizontal) {
      m_text_scroll_offset = std::clamp(
          m_text_scroll_offset + static_cast<float>(line_delta) * 15.0F, 0.0F,
          std::max(0.0F, m_max_text_scroll));
      return true;
    }
    m_scrollbar.synchronize(
        count_visible_lines(m_folding, doc->get_line_count()), vis_lines);
    m_reveal_caret_pending = false;
    return m_scrollbar.scroll_lines(line_delta);
  }

  return false;
}

bool TextEditor::handle_input(UI::Editor::EditorInputCommand command,
                              bool extend_selection) {
  {
    std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
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
          if (auto *doc = get_focused_document(); doc != nullptr) {
            const std::string_view current_line =
                doc->get_line(doc->get_caret_line());
            const std::size_t caret_col = doc->get_caret_column();

            // Detect if we are in an #include context
            bool in_include_ctx = false;
            {
              const auto inc_pos = current_line.find("#include");
              const auto imp_pos = current_line.find("#import");
              if (inc_pos != std::string_view::npos ||
                  imp_pos != std::string_view::npos) {
                in_include_ctx = true;
              }
            }

            std::size_t word_start = std::min(caret_col, current_line.size());
            if (in_include_ctx) {
              // In #include context, only delete the filename part after the
              // last '/'
              while (word_start > 0) {
                const char c = current_line[word_start - 1];
                if (c == '/' || c == '<' || c == '"') {
                  break; // Stop at path delimiter
                }
                --word_start;
              }
            } else {
              while (word_start > 0) {
                const char c = current_line[word_start - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                    c == '#' || c == '~') {
                  --word_start;
                } else {
                  break;
                }
              }
            }

            const std::size_t prefix_len = caret_col - word_start;
            for (std::size_t k = 0; k < prefix_len; ++k) {
              static_cast<void>(m_controller.execute_input(
                  UI::Editor::EditorInputCommand::DeleteBackward));
            }

            std::string text_to_insert =
                item->insert_text.empty() ? item->label : item->insert_text;
            if (!text_to_insert.empty() && (text_to_insert.back() == '>' ||
                                            text_to_insert.back() == '"')) {
              const std::size_t cur_col = doc->get_caret_column();
              const auto cur_line_view = doc->get_line(doc->get_caret_line());
              if (cur_col < cur_line_view.size() &&
                  cur_line_view[cur_col] == text_to_insert.back()) {
                text_to_insert.pop_back();
              }
            }
            const std::size_t start_line = doc->get_caret_line();
            const std::size_t start_col = doc->get_caret_column();

            std::size_t target_cursor_line = start_line;
            std::size_t target_cursor_col = start_col;
            bool cursor_targeted = false;

            // 1. Find all placeholders ${N:default_val} and replace all
            // instances
            std::size_t first_placeholder_pos = std::string::npos;
            while (true) {
              const std::size_t p_start = text_to_insert.find("${");
              if (p_start == std::string::npos)
                break;
              const std::size_t p_end = text_to_insert.find('}', p_start);
              if (p_end == std::string::npos)
                break;

              const std::string inner =
                  text_to_insert.substr(p_start + 2, p_end - (p_start + 2));
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

            // 2. Handle $0 tabstop
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

            const std::string fname = std::string(doc->get_file_name());
            const std::string uri = get_active_document_uri();
            std::string content;
            for (std::size_t li = 0; li < doc->get_line_count(); ++li) {
              content += doc->get_line(li);
              content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_changed(
                uri, fname, 1, content);
            return true;
          }
        }
      }
    }
  }

  bool handled = false;
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value()) {
    if (auto *right_doc = m_controller.get_document(*m_split_document_index)) {
      handled = right_doc->execute(command, extend_selection);
    }
  } else {
    handled = m_controller.execute_input(command, extend_selection);
  }

  if (handled) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();

    if (auto *doc = get_focused_document()) {
      const std::string fname = std::string(doc->get_file_name());
      const std::string uri = get_active_document_uri();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_changed(
          uri, fname, 1, content);

      // Auto-hide signature help on newline, cursor movement, or outside parens
      if (command == UI::Editor::EditorInputCommand::InsertNewLine ||
          command == UI::Editor::EditorInputCommand::MoveUp ||
          command == UI::Editor::EditorInputCommand::MoveDown ||
          command == UI::Editor::EditorInputCommand::MoveHome ||
          command == UI::Editor::EditorInputCommand::MoveEnd ||
          command == UI::Editor::EditorInputCommand::Escape) {
        m_signature_help.hide();
      } else if (m_signature_help.is_visible()) {
        const std::string_view current_line =
            doc->get_line(doc->get_caret_line());
        const std::size_t caret_col = doc->get_caret_column();
        const std::string_view prefix =
            current_line.substr(0, std::min(caret_col, current_line.size()));

        std::size_t open_count = 0;
        std::size_t close_count = 0;
        for (char ch : prefix) {
          if (ch == '(')
            ++open_count;
          else if (ch == ')')
            ++close_count;
        }

        if (open_count <= close_count) {
          m_signature_help.hide();
        }
      }

      // If Backspace or Delete occurred while completion popup was open,
      // auto-close or re-filter
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

        if (current_word.empty() || current_word == "#" || caret_col == 0) {
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
  return handled;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action) {
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value()) {
    if (auto *right_doc = m_controller.get_document(*m_split_document_index)) {
      bool changed = false;
      switch (action) {
      case UI::Editor::EditorAction::SelectAll:
        changed = right_doc->select_all();
        break;
      case UI::Editor::EditorAction::ToggleComment:
        changed = right_doc->toggle_line_comment();
        break;
      case UI::Editor::EditorAction::MoveLineUp:
        changed = right_doc->move_line_up();
        break;
      case UI::Editor::EditorAction::MoveLineDown:
        changed = right_doc->move_line_down();
        break;
      case UI::Editor::EditorAction::AddCursorAbove:
        changed = right_doc->add_cursor_above();
        break;
      case UI::Editor::EditorAction::AddCursorBelow:
        changed = right_doc->add_cursor_below();
        break;
      default:
        break;
      }
      if (changed) {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
      }
      return changed;
    }
  }
  return m_controller.execute_action(action);
}

std::optional<bool> TextEditor::handle_command(std::string_view command_id) {
  if (command_id == "zde.editor.split_right" ||
      command_id == Commands::CommandIds::view_split_right) {
    const auto doc_count = m_controller.get_documents().size();
    if (doc_count > 1) {
      const std::size_t active = m_controller.get_active_index().value_or(0);
      m_is_split = true;
      m_split_document_index = (active + 1) % doc_count;
      m_focused_pane = SplitPaneFocus::Right;
    } else if (doc_count == 1) {
      m_is_split = true;
      m_split_document_index = m_controller.get_active_index().value_or(0);
      m_focused_pane = SplitPaneFocus::Right;
    }
    return true;
  }
  if (command_id == "zde.editor.close_split") {
    m_is_split = false;
    m_split_document_index.reset();
    m_focused_pane = SplitPaneFocus::Left;
    return true;
  }
  if (command_id == "zde.editor.focus_first_group") {
    m_focused_pane = SplitPaneFocus::Left;
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    return true;
  }
  if (command_id == "zde.editor.focus_second_group") {
    if (m_is_split && m_split_document_index.has_value()) {
      m_focused_pane = SplitPaneFocus::Right;
      m_reveal_caret_pending = true;
      m_caret_blink.reset();
    }
    return true;
  }
  if (command_id == "workbench.action.closeActiveEditor") {
    if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
        m_split_document_index.has_value()) {
      m_is_split = false;
      m_split_document_index.reset();
      m_focused_pane = SplitPaneFocus::Left;
      return true;
    }
    if (const auto active = m_controller.get_active_index()) {
      static_cast<void>(m_controller.close_file(*active));
      if (m_is_split && m_controller.get_documents().size() <= 1) {
        m_is_split = false;
        m_split_document_index.reset();
        m_focused_pane = SplitPaneFocus::Left;
      }
      return true;
    }
    return false;
  }
  if (command_id == "editor.action.triggerSuggest" ||
      command_id == "zde.editor.triggerSuggest") {
    if (auto *doc = get_focused_document()) {
      const std::string fname = std::string(doc->get_file_name());
      const std::string uri = get_active_document_uri();
      const std::string_view current_line =
          doc->get_line(doc->get_caret_line());
      Language::Protocol::Position pos{.line = doc->get_caret_line(),
                                       .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_completion(
          uri, fname, pos, current_line,
          [this](std::vector<Language::Protocol::CompletionItem> items) {
            std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
            if (!items.empty()) {
              m_completion_popup.show(std::move(items), 100.0F, 100.0F);
            } else {
              m_completion_popup.hide();
            }
            m_lsp_dirty = true;
          });
      return true;
    }
  }
  if (command_id == Commands::CommandIds::editor_goto_definition ||
      command_id == Commands::CommandIds::edit_goto_definition ||
      command_id == "editor.action.revealDefinition" ||
      command_id == "zde.editor.gotoDefinition") {
    return go_to_definition();
  }
  if (const auto action =
          UI::Editor::EditorController::action_from_command_id(command_id)) {
    return m_controller.execute_action(*action);
  }
  return std::nullopt;
}

std::optional<bool>
TextEditor::is_command_enabled(std::string_view command_id) const noexcept {
  if (command_id == "zde.editor.split_right" ||
      command_id == Commands::CommandIds::view_split_right) {
    return !m_controller.get_documents().empty();
  }
  if (command_id == "zde.editor.close_split") {
    return m_is_split;
  }
  if (command_id == Commands::CommandIds::editor_goto_definition ||
      command_id == Commands::CommandIds::edit_goto_definition ||
      command_id == "editor.action.revealDefinition" ||
      command_id == "zde.editor.gotoDefinition") {
    return get_focused_document() != nullptr;
  }
  if (const auto action =
          UI::Editor::EditorController::action_from_command_id(command_id)) {
    return m_controller.can_execute_action(*action);
  }
  return std::nullopt;
}

bool TextEditor::handle_text_input(std::string_view utf8_text) {
  auto *doc = get_focused_document();
  if (doc == nullptr) {
    return false;
  }

  const std::size_t initial_caret_line = doc->get_caret_line();
  const std::size_t initial_caret_col = doc->get_caret_column();
  const std::string_view initial_line = doc->get_line(initial_caret_line);

  // 1. Skip-over existing closing character when typed
  if ((utf8_text == ")" || utf8_text == "]" || utf8_text == "}" ||
       utf8_text == "\"" || utf8_text == "'") &&
      initial_caret_col < initial_line.size() &&
      initial_line[initial_caret_col] == utf8_text[0]) {
    doc->set_caret(initial_caret_line, initial_caret_col + 1);
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_completion_popup.hide();
    m_signature_help.hide();
    return true;
  }

  // 2. Smart auto-closing for (, [, ", '
  if (utf8_text == "(" || utf8_text == "[" || utf8_text == "\"" ||
      utf8_text == "'") {
    std::string pair_text;
    if (utf8_text == "(")
      pair_text = "()";
    else if (utf8_text == "[")
      pair_text = "[]";
    else if (utf8_text == "\"")
      pair_text = "\"\"";
    else if (utf8_text == "'")
      pair_text = "''";

    const bool changed = doc->insert_text(pair_text);
    if (changed) {
      doc->set_caret(initial_caret_line, initial_caret_col + 1);
      m_reveal_caret_pending = true;
      m_caret_blink.reset();

      const std::string fname = std::string(doc->get_file_name());
      const std::string uri = get_active_document_uri();
      std::string content;
      for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
        content += doc->get_line(i);
        content += "\n";
      }
      Language::LanguageServerManager::instance().on_document_changed(
          uri, fname, 1, content);

      if (utf8_text == "(") {
        Language::Protocol::Position sig_pos{.line = doc->get_caret_line(),
                                             .character =
                                                 doc->get_caret_column()};
        Language::LanguageServerManager::instance().request_signature_help(
            uri, fname, sig_pos, doc->get_line(doc->get_caret_line()),
            [this](std::optional<Language::Protocol::SignatureHelp> help) {
              std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
              if (help.has_value() && !help->signatures.empty()) {
                m_signature_help.show(std::move(*help), 0.0F, 0.0F);
              } else {
                m_signature_help.hide();
              }
              m_lsp_dirty = true;
            });
      }

      Language::Protocol::Position pos{.line = doc->get_caret_line(),
                                       .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_completion(
          uri, fname, pos, doc->get_line(doc->get_caret_line()),
          [this](std::vector<Language::Protocol::CompletionItem> items) {
            std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
            if (!items.empty()) {
              m_completion_popup.show(std::move(items), 100.0F, 100.0F);
            }
            m_lsp_dirty = true;
          });
      return true;
    }
  }

  const bool changed = doc->insert_text(utf8_text);
  if (changed) {
    m_reveal_caret_pending = true;
    m_caret_blink.reset();

    const std::string fname = std::string(doc->get_file_name());
    const std::string uri = get_active_document_uri();
    std::string content;
    for (std::size_t i = 0; i < doc->get_line_count(); ++i) {
      content += doc->get_line(i);
      content += "\n";
    }
    Language::LanguageServerManager::instance().on_document_changed(uri, fname,
                                                                    1, content);

    const std::string_view current_line = doc->get_line(doc->get_caret_line());
    const std::size_t caret_col = doc->get_caret_column();

    // Extract the active word token before cursor
    std::size_t word_start = std::min(caret_col, current_line.size());
    while (word_start > 0) {
      const char c = current_line[word_start - 1];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#' ||
          c == '~') {
        --word_start;
      } else {
        break;
      }
    }
    const std::string_view current_word =
        current_line.substr(word_start, caret_col - word_start);

    const bool is_include_context =
        current_line.find('#') != std::string_view::npos || utf8_text == "<" ||
        utf8_text == "\"" || utf8_text == "#";
    const bool is_trigger_char = utf8_text == "." || utf8_text == ">" ||
                                 utf8_text == ":" || utf8_text == "/" ||
                                 utf8_text == "\\" || is_include_context ||
                                 current_word.size() >= 3;

    if (utf8_text == "{") {
      std::string_view line_before =
          current_line.substr(0, caret_col > 0 ? caret_col - 1 : 0);
      while (!line_before.empty() &&
             std::isspace(static_cast<unsigned char>(line_before.back()))) {
        line_before.remove_suffix(1);
      }

      const bool is_type =
          line_before.find("struct ") != std::string_view::npos ||
          line_before.find("struct\t") != std::string_view::npos ||
          line_before.find("class ") != std::string_view::npos ||
          line_before.find("class\t") != std::string_view::npos ||
          line_before.find("enum ") != std::string_view::npos ||
          line_before.find("union ") != std::string_view::npos ||
          line_before.starts_with("struct") || line_before.starts_with("class");
      const bool is_namespace =
          line_before.find("namespace ") != std::string_view::npos ||
          line_before.starts_with("namespace");

      if (is_type) {
        static_cast<void>(m_controller.insert_text("\n    \n};"));
        const std::size_t cur_line = doc->get_caret_line();
        if (cur_line > 0) {
          doc->set_caret(cur_line - 1, 4);
        }
        std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
        m_completion_popup.hide();

        std::string content;
        for (std::size_t li = 0; li < doc->get_line_count(); ++li) {
          content += doc->get_line(li);
          content += "\n";
        }
        Language::LanguageServerManager::instance().on_document_changed(
            uri, fname, 1, content);
        return true;
      } else if (is_namespace) {
        const std::size_t ns_pos = line_before.find("namespace");
        std::string ns_name;
        if (ns_pos != std::string_view::npos &&
            ns_pos + 9 < line_before.size()) {
          ns_name = std::string(line_before.substr(ns_pos + 9));
          while (!ns_name.empty() &&
                 std::isspace(static_cast<unsigned char>(ns_name.front()))) {
            ns_name.erase(0, 1);
          }
          while (!ns_name.empty() &&
                 std::isspace(static_cast<unsigned char>(ns_name.back()))) {
            ns_name.pop_back();
          }
        }
        const std::string closing =
            ns_name.empty() ? "\n\n\n}" : ("\n\n\n} // namespace " + ns_name);
        static_cast<void>(m_controller.insert_text(closing));
        const std::size_t cur_line = doc->get_caret_line();
        if (cur_line >= 2) {
          doc->set_caret(cur_line - 2, 0);
        }
        std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
        m_completion_popup.hide();

        std::string content;
        for (std::size_t li = 0; li < doc->get_line_count(); ++li) {
          content += doc->get_line(li);
          content += "\n";
        }
        Language::LanguageServerManager::instance().on_document_changed(
            uri, fname, 1, content);
        return true;
      }
    }

    // Check if caret is inside parentheses '(' ... ')'
    const std::string_view prefix_before_caret =
        current_line.substr(0, std::min(caret_col, current_line.size()));
    std::size_t open_parens = 0;
    std::size_t close_parens = 0;
    for (char ch : prefix_before_caret) {
      if (ch == '(')
        ++open_parens;
      else if (ch == ')')
        ++close_parens;
    }

    const bool is_inside_parens = (open_parens > close_parens);

    if (is_inside_parens &&
        (utf8_text == "(" || utf8_text == "," ||
         m_signature_help.is_visible() || !current_word.empty())) {
      Language::Protocol::Position sig_pos{
          .line = doc->get_caret_line(), .character = doc->get_caret_column()};
      Language::LanguageServerManager::instance().request_signature_help(
          uri, fname, sig_pos, current_line,
          [this](std::optional<Language::Protocol::SignatureHelp> help) {
            std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
            if (help.has_value() && !help->signatures.empty()) {
              m_signature_help.show(std::move(*help), 0.0F, 0.0F);
            } else {
              m_signature_help.hide();
            }
            m_lsp_dirty = true;
          });
    } else {
      std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
      m_signature_help.hide();
    }

    if (utf8_text == " " || utf8_text == ";" || utf8_text == ")" ||
        utf8_text == "}") {
      std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
      m_completion_popup.hide();
    } else if (is_trigger_char || m_completion_popup.is_visible() ||
               current_word.size() >= 2 ||
               (!current_line.empty() &&
                current_line.find('#') != std::string_view::npos)) {
      std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
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
        if (lt != std::string_view::npos &&
            (qt == std::string_view::npos || lt > qt)) {
          is_include_context = true;
          header_query = std::string(after_inc.substr(lt + 1));
        } else if (qt != std::string_view::npos) {
          is_include_context = true;
          header_query = std::string(after_inc.substr(qt + 1));
        }
      }

      std::string active_query;
      if (is_include_context) {
        const auto last_slash = header_query.rfind('/');
        if (last_slash != std::string::npos) {
          active_query = header_query.substr(last_slash + 1);
        } else {
          active_query = header_query;
        }
      } else {
        active_query = std::string(current_word);
      }

      // Synchronously update filter immediately with zero latency
      if (m_completion_popup.is_visible()) {
        if (is_include_context && (utf8_text == "/" || utf8_text == "\\" ||
                                   utf8_text == "<" || utf8_text == "\"")) {
          // Re-scan folder-scoped headers when descending into sub-folders
          auto header_items =
              Language::LanguageServerManager::get_header_completions(
                  line_prefix, std::filesystem::current_path());
          if (!header_items.empty()) {
            m_completion_popup.show(std::move(header_items), 0.0F, 0.0F);
          }
        }
        m_completion_popup.set_filter(active_query);
      } else {
        std::vector<Language::Protocol::CompletionItem> local_items;

        if (is_include_context) {
          // In #include / #import context, prioritize header libraries &
          // workspace files in the target directory
          local_items = Language::LanguageServerManager::get_header_completions(
              line_prefix, std::filesystem::current_path());
        } else {
          // Instant Local Suggestions (Language Templates & Buffer Identifiers)
          auto templates =
              Language::LanguageServerManager::get_templates_for_filename(
                  fname);
          for (auto &tpl : templates) {
            local_items.push_back(std::move(tpl));
          }

          // Extract tokens from current document (capped for instant responsiveness)
          std::unordered_set<std::string> doc_words;
          const std::size_t max_scan_lines = std::min(doc->get_line_count(), std::size_t{300});
          for (std::size_t li = 0; li < max_scan_lines && doc_words.size() < 120; ++li) {
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
            std::lock_guard<std::recursive_mutex> lk(m_lsp_mutex);
            if (!items.empty()) {
              if (m_completion_popup.is_visible()) {
                m_completion_popup.merge_items(std::move(items));
              } else {
                m_completion_popup.show(std::move(items), 0.0F, 0.0F);
              }

              if (m_completion_popup.get_item_count() == 0) {
                m_completion_popup.hide();
              }
            }
            m_lsp_dirty = true;
          });
    }
  }
  return changed;
}

bool TextEditor::is_focused() const noexcept { return m_focused; }

UI::Editor::TextDocumentModel *TextEditor::get_focused_document() noexcept {
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    return m_controller.get_document(*m_split_document_index);
  }
  return m_controller.get_active_document();
}

const UI::Editor::TextDocumentModel *
TextEditor::get_focused_document() const noexcept {
  if (m_is_split && m_focused_pane == SplitPaneFocus::Right &&
      m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    return m_controller.get_document(*m_split_document_index);
  }
  return m_controller.get_active_document();
}

bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scrollbar_w = 12.0F * scale;
    const UI::Rect left_scrollbar{splitter_x - scrollbar_w,
                                  layout.editor_bounds.y, scrollbar_w,
                                  layout.editor_bounds.height};
    const UI::Rect right_scrollbar{layout.editor_bounds.right() - scrollbar_w,
                                   layout.editor_bounds.y, scrollbar_w,
                                   layout.editor_bounds.height};
    return left_scrollbar.contains(point_x, point_y) ||
           right_scrollbar.contains(point_x, point_y);
  }
  return m_scrollbar.is_point(layout, point_x, point_y);
}

bool TextEditor::is_minimap_point(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const float scrollbar_w = 12.0F * scale;
    const float minimap_w = 50.0F * scale;
    const UI::Rect left_minimap{splitter_x - scrollbar_w - minimap_w,
                                layout.editor_bounds.y, minimap_w,
                                layout.editor_bounds.height};
    const UI::Rect right_minimap{
        layout.editor_bounds.right() - scrollbar_w - minimap_w,
        layout.editor_bounds.y, minimap_w, layout.editor_bounds.height};
    return left_minimap.contains(point_x, point_y) ||
           right_minimap.contains(point_x, point_y);
  }
  return m_minimap.is_point(layout, point_x, point_y);
}

bool TextEditor::check_external_file_changes() {
  const auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(
          now - m_last_file_check_time)
          .count() < 1000) {
    return false;
  }
  m_last_file_check_time = now;
  const auto reloaded = m_controller.reload_externally_modified_files();
  return !reloaded.empty();
}

bool TextEditor::tick_animations() noexcept {
  const bool has_doc = get_document() != nullptr;
  bool needs_repaint = (m_focused && has_doc) ? m_caret_blink.tick() : false;
  needs_repaint |= m_brace_animation.tick();
  {
    std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
    if (m_lsp_dirty) {
      m_lsp_dirty = false;
      needs_repaint = true;
    }
  }

  // Smooth lerp animated tab sliding offsets (smooth sliding when tabs are closed)
  bool animating = false;
  for (auto &[id, offset_x] : m_tab_animated_offset_x) {
    if (std::abs(offset_x) > 0.5F) {
      offset_x += (0.0F - offset_x) * 0.3F;
      animating = true;
    } else {
      offset_x = 0.0F;
    }
  }
  if (animating) {
    needs_repaint = true;
  }

  return needs_repaint;
}

const UI::Editor::TextDocumentModel *TextEditor::get_document() const noexcept {
  return get_focused_document();
}

void TextEditor::render(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  draw_tab_strip(surface, drawable, layout);
  if (!layout.editor_header_bounds.is_empty() &&
      layout.editor_header_bounds.height > 2.0F) {
    draw_editor_header(surface, drawable, layout);
  }
  if (layout.editor_bounds.is_empty() || layout.editor_bounds.height <= 2.0F) {
    return;
  }
  draw_document(surface, drawable, layout);

  if (!m_is_split) {
    if (const UI::Editor::TextDocumentModel *document =
            m_controller.get_active_document()) {
      const float line_height = 20.0F * surface.m_dpi_scale;
      const std::size_t visible_count = static_cast<std::size_t>(std::max(
          static_cast<int>(layout.editor_bounds.height / line_height), 1));
      m_scrollbar.render(surface, drawable, layout);

      // Render Scrollbar Error / Warning Stripes (Overview Ruler)
      const std::size_t total_lines = document->get_line_count();
      if (total_lines > 0) {
        const auto all_diags = document->get_diagnostics();
        const float track_x = layout.scrollbar_bounds.x;
        const float track_y = layout.scrollbar_bounds.y;
        const float track_w = layout.scrollbar_bounds.width;
        const float track_h = layout.scrollbar_bounds.height;

        for (const auto &d : all_diags) {
          const std::size_t line_idx = d.range.start.line;
          if (line_idx >= total_lines)
            continue;

          const bool has_err =
              (d.severity == Language::Protocol::DiagnosticSeverity::Error);
          const bool has_warn =
              (d.severity == Language::Protocol::DiagnosticSeverity::Warning);

          const float stripe_y = track_y + (static_cast<float>(line_idx) /
                                            static_cast<float>(total_lines)) *
                                               track_h;
          const UI::Theme::Color stripe_color =
              has_err ? UI::Theme::Color{247, 84, 100, 255}
                      : (has_warn ? UI::Theme::Color{240, 167, 50, 255}
                                  : UI::Theme::Color{86, 182, 194, 255});

          surface.fill_rectangle(
              drawable,
              UI::Rect{track_x + 1.0F, stripe_y, track_w - 2.0F,
                       std::max(2.5F * surface.m_dpi_scale, 2.0F)},
              surface.allocate_color(stripe_color));
        }
      }

      m_minimap.render(surface, drawable, layout, *document,
                       m_scrollbar.get_first_visible_line(), visible_count);
    }
  } else if (m_split_document_index.has_value() &&
             *m_split_document_index < m_controller.get_documents().size()) {
    const float scale = surface.m_dpi_scale;
    const float line_height = 20.0F * scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const float splitter_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    // --- Left Pane Minimap & Scrollbar ---
    const float scroll_top_y = layout.editor_bounds.y;
    const float scroll_total_h = layout.editor_bounds.height;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y,
                               splitter_x - layout.editor_bounds.x,
                               scroll_total_h};
    const float left_minimap_w =
        (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w,
                                  scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y,
                                left_minimap_w, scroll_total_h};

    UI::Editor::StudioEditorLayoutResult left_layout = layout;
    left_layout.minimap_bounds = left_minimap;
    left_layout.scrollbar_bounds = left_scrollbar;

    if (const UI::Editor::TextDocumentModel *left_doc =
            m_controller.get_active_document()) {
      m_scrollbar.render(surface, drawable, left_layout);

      const std::size_t total_lines = left_doc->get_line_count();
      if (total_lines > 0) {
        const auto all_diags = left_doc->get_diagnostics();
        for (const auto &d : all_diags) {
          const std::size_t line_idx = d.range.start.line;
          if (line_idx >= total_lines)
            continue;
          const bool has_err =
              (d.severity == Language::Protocol::DiagnosticSeverity::Error);
          const bool has_warn =
              (d.severity == Language::Protocol::DiagnosticSeverity::Warning);

          const float stripe_y =
              left_scrollbar.y +
              (static_cast<float>(line_idx) / static_cast<float>(total_lines)) *
                  left_scrollbar.height;
          const UI::Theme::Color stripe_color =
              has_err ? UI::Theme::Color{247, 84, 100, 255}
                      : (has_warn ? UI::Theme::Color{240, 167, 50, 255}
                                  : UI::Theme::Color{86, 182, 194, 255});
          surface.fill_rectangle(drawable,
                                 UI::Rect{left_scrollbar.x + 1.0F, stripe_y,
                                          left_scrollbar.width - 2.0F,
                                          std::max(2.5F * scale, 2.0F)},
                                 surface.allocate_color(stripe_color));
        }
      }

      if (!left_minimap.is_empty()) {
        m_minimap.render(surface, drawable, left_layout, *left_doc,
                         m_scrollbar.get_first_visible_line(), visible_count);
      }
    }

    // --- Right Pane Minimap & Scrollbar ---
    const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y,
                                layout.editor_bounds.right() -
                                    (splitter_x + 2.0F * scale),
                                scroll_total_h};
    const float right_minimap_w =
        (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
            ? (FIXED_MINIMAP_WIDTH * scale)
            : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w,
                                   scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w,
                                 scroll_top_y, right_minimap_w, scroll_total_h};

    UI::Editor::StudioEditorLayoutResult right_layout = layout;
    right_layout.minimap_bounds = right_minimap;
    right_layout.scrollbar_bounds = right_scrollbar;

    if (const UI::Editor::TextDocumentModel *right_doc =
            m_controller.get_document(*m_split_document_index)) {
      m_split_scrollbar.render(surface, drawable, right_layout);

      const std::size_t total_lines = right_doc->get_line_count();
      if (total_lines > 0) {
        const auto all_diags = right_doc->get_diagnostics();
        for (const auto &d : all_diags) {
          const std::size_t line_idx = d.range.start.line;
          if (line_idx >= total_lines)
            continue;
          const bool has_err =
              (d.severity == Language::Protocol::DiagnosticSeverity::Error);
          const bool has_warn =
              (d.severity == Language::Protocol::DiagnosticSeverity::Warning);

          const float stripe_y =
              right_scrollbar.y +
              (static_cast<float>(line_idx) / static_cast<float>(total_lines)) *
                  right_scrollbar.height;
          const UI::Theme::Color stripe_color =
              has_err ? UI::Theme::Color{247, 84, 100, 255}
                      : (has_warn ? UI::Theme::Color{240, 167, 50, 255}
                                  : UI::Theme::Color{86, 182, 194, 255});
          surface.fill_rectangle(drawable,
                                 UI::Rect{right_scrollbar.x + 1.0F, stripe_y,
                                          right_scrollbar.width - 2.0F,
                                          std::max(2.5F * scale, 2.0F)},
                                 surface.allocate_color(stripe_color));
        }
      }

      if (!right_minimap.is_empty()) {
        m_split_minimap.render(surface, drawable, right_layout, *right_doc,
                               m_split_scrollbar.get_first_visible_line(),
                               visible_count);
      }
    }

    // Draw continuous split divider line on TOP of minimap and scrollbar (full height from tab bar to editor bottom)
    const bool split_highlight = m_is_resizing_split || m_hovered_split_resize;
    const unsigned long divider_col = split_highlight
                                          ? surface.m_pixels.accent
                                          : surface.m_pixels.border;
    const float divider_top_y = (!layout.editor_header_bounds.is_empty() && layout.editor_header_bounds.height > 2.0F)
                                    ? layout.editor_header_bounds.y
                                    : layout.tab_bar_bounds.y;
    const float divider_total_h = layout.editor_bounds.bottom() - divider_top_y;
    surface.fill_rectangle(drawable,
                           UI::Rect{splitter_x, divider_top_y,
                                    2.0F * scale,
                                    divider_total_h},
                           divider_col);
  }

  draw_split_drop_overlay(surface, drawable, layout);
  draw_diagnostic_hover_overlay(surface, drawable, layout);
  draw_signature_help(surface, drawable, layout);
  draw_completion_popup(surface, drawable, layout);
  draw_hover_tooltip(surface, drawable, layout);
  draw_tab_action_menu(surface, drawable, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const float scale = surface.m_dpi_scale;
  const auto documents = m_controller.get_documents();

  float total_width = 0.0F;
  for (std::size_t index = 0; index < documents.size(); ++index) {
    const std::string_view name = documents[index].text.get_file_name();
    const float text_w =
        static_cast<float>(surface.m_ui_font->getTextWidth(name));
    total_width += UI::Editor::calculate_editor_tab_width(text_w, scale);
    total_width += UI::Editor::StudioEditorMetrics::editor_tab_gap * scale;
  }

  m_max_tab_scroll = std::max(0.0F, total_width - layout.tab_bar_bounds.width);
  if (m_max_tab_scroll == 0.0F) {
    const_cast<TextEditor *>(this)->m_tab_scroll_offset = 0.0F;
  }

  surface.push_clip(layout.tab_bar_bounds);

  m_tab_count = 0;
  float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
  const float right_limit = layout.tab_bar_bounds.right();
  const std::optional<std::size_t> active_index =
      m_controller.get_active_index();

  for (std::size_t index = 0; index < documents.size(); ++index) {
    const auto &document = documents[index].text;
    const std::string_view name = document.get_file_name();
    const float text_w =
        static_cast<float>(surface.m_ui_font->getTextWidth(name));
    const float width = UI::Editor::calculate_editor_tab_width(text_w, scale);
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
    tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap * scale;
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
    const auto &document = documents[index].text;
    const bool active = active_index && *active_index == index;
    const UI::Rect &bounds = m_tab_bounds[tab_index];

    // Completely skip if tab is completely out of visible tab bar bounds
    if (bounds.x >= right_limit || bounds.right() <= layout.tab_bar_bounds.x) {
      return;
    }

    const bool close_hovered =
        m_hovered_tab_close_index && *m_hovered_tab_close_index == tab_index;
    const bool tab_hovered =
        m_hovered_tab_index && *m_hovered_tab_index == tab_index;
    const bool is_dragging_this = m_tab_drag_drop.is_dragging() &&
                                  m_tab_drag_drop.get_dragged_index() == index;

    if (is_dragging_this) {
      const UI::Rect shadow_rect{bounds.x + 2.0F * scale,
                                 bounds.y + 2.0F * scale, bounds.width,
                                 bounds.height};
      surface.fill_rounded_rectangle(
          drawable, shadow_rect,
          surface.allocate_color(UI::Theme::Color{20, 22, 28, 255}),
          4.0F * scale, surface.m_pixels.tab_background);
    }

    const int tab_left =
        std::max(round_to_int(bounds.x), round_to_int(layout.tab_bar_bounds.x));
    const int tab_right = std::min(round_to_int(bounds.right()) - 1,
                                   round_to_int(right_limit) - 1);
    if (tab_left >= tab_right) {
      return;
    }
    const int tab_top = round_to_int(bounds.y);
    const int tab_bottom = round_to_int(bounds.bottom()) - 1;

    const UI::Rect visible_bounds{static_cast<float>(tab_left), bounds.y,
                                  static_cast<float>(tab_right - tab_left + 1),
                                  bounds.height};

    surface.fill_rectangle(
        drawable, visible_bounds,
        is_dragging_this
            ? surface.allocate_color(UI::Theme::Color{38, 42, 50, 255})
            : (active ? surface.m_pixels.tab_active_background
                      : (tab_hovered ? surface.m_pixels.hover_background
                                     : surface.m_pixels.tab_background)));

    if (active && !is_dragging_this) {
      // 1. Top accent bar (Atas)
      const float bar_h = std::max(2.0F * scale, 2.0F);
      const UI::Rect active_top_bar{
          static_cast<float>(tab_left), static_cast<float>(tab_top),
          static_cast<float>(tab_right - tab_left + 1), bar_h};
      surface.fill_rectangle(drawable, active_top_bar, surface.m_pixels.accent);

      // 2. Left border (Kiri)
      if (bounds.x >= layout.tab_bar_bounds.x) {
        surface.draw_line(drawable, tab_left, tab_top, tab_left, tab_bottom,
                          surface.m_pixels.border);
      }
      // 3. Right border (Kanan)
      if (bounds.right() <= right_limit) {
        surface.draw_line(drawable, tab_right, tab_top, tab_right, tab_bottom,
                          surface.m_pixels.border);
      }
      // 4. Bottom (Bawah): terbuka / menyatu langsung ke editor, tidak digambar garis
    } else {
      const unsigned long tab_edge_color =
          is_dragging_this ? surface.m_pixels.accent : surface.m_pixels.border;

      // Inactive tab: top rule
      surface.draw_line(drawable, tab_left, tab_top, tab_right, tab_top,
                        tab_edge_color);
      // Vertical side separators
      if (bounds.x >= layout.tab_bar_bounds.x) {
        surface.draw_line(drawable, tab_left, tab_top, tab_left, tab_bottom,
                          tab_edge_color);
      }
      if (bounds.right() <= right_limit) {
        surface.draw_line(drawable, tab_right, tab_top, tab_right, tab_bottom,
                          tab_edge_color);
      }
      // Bottom rule (inactive tab closed at bottom)
      surface.draw_line(drawable, tab_left, tab_bottom, tab_right, tab_bottom,
                        tab_edge_color);
    }

    const UI::Theme::Color &tab_bg_color =
        active ? surface.m_palette.tab_active_background
               : (tab_hovered ? surface.m_palette.hover_background
                              : surface.m_palette.tab_background);

    const std::string icon_asset =
        UI::Editor::file_icon_asset_for_path(document.get_file_name());
    const int icon_cx = round_to_int(
        bounds.x +
        (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
            scale);
    const int icon_sz = std::max(round_to_int(14.0F * scale), 10);
    if (icon_cx - icon_sz / 2 >= layout.tab_bar_bounds.x &&
        icon_cx + icon_sz / 2 <= right_limit) {
      surface.draw_svg_icon(drawable, "Assets/icons/" + icon_asset, icon_cx,
                            round_to_int(bounds.y + bounds.height * 0.5F),
                            icon_sz, surface.m_palette.text_primary,
                            tab_bg_color, true);
    }

    surface.draw_text(
        drawable, *surface.m_ui_font, document.get_file_name(),
        bounds.x +
            UI::Editor::StudioEditorMetrics::editor_tab_label_offset * scale,
        bounds.y + bounds.height * 0.5F,
        active ? surface.m_text.primary : surface.m_text.muted,
        &layout.tab_bar_bounds);

    const float close_cx =
        bounds.right() -
        UI::Editor::StudioEditorMetrics::editor_tab_close_width * 0.5F * scale;
    const float close_cy = bounds.y + bounds.height * 0.5F;
    const int close_icon_sz = std::max(round_to_int(12.0F * scale), 10);

    if (close_cx - close_icon_sz * 0.5F >= layout.tab_bar_bounds.x &&
        close_cx + close_icon_sz * 0.5F <= right_limit) {
      if (document.is_dirty() && !close_hovered && !tab_hovered) {
        surface.draw_svg_icon(drawable, "Assets/icons/dirty.svg",
                              round_to_int(close_cx), round_to_int(close_cy),
                              std::max(round_to_int(10.0F * scale), 8),
                              surface.m_palette.warning, tab_bg_color);
      } else if (active || tab_hovered || close_hovered) {
        UI::Theme::Color close_bg = tab_bg_color;
        if (close_hovered) {
          const float pad = 2.0F * scale;
          const UI::Rect btn_rect{
              close_cx - close_icon_sz * 0.5F - pad,
              close_cy - close_icon_sz * 0.5F - pad,
              static_cast<float>(close_icon_sz) + pad * 2.0F,
              static_cast<float>(close_icon_sz) + pad * 2.0F,
          };
          const std::uint32_t a = 35; // 14% subtle white hover
          close_bg = UI::Theme::Color{
              static_cast<std::uint8_t>(
                  (255 * a + tab_bg_color.red * (255 - a) + 127) / 255),
              static_cast<std::uint8_t>(
                  (255 * a + tab_bg_color.green * (255 - a) + 127) / 255),
              static_cast<std::uint8_t>(
                  (255 * a + tab_bg_color.blue * (255 - a) + 127) / 255),
              255};
          surface.fill_rounded_rectangle(
              drawable, btn_rect, surface.allocate_color(close_bg),
              3.0F * scale,
              active ? surface.m_pixels.tab_active_background
                     : surface.m_pixels.tab_background);
        }
        const UI::Theme::Color close_col =
            close_hovered ? UI::Theme::Color{255, 255, 255, 255}
                          : (active ? surface.m_palette.text_primary
                                    : surface.m_palette.text_muted);
        surface.draw_svg_icon(drawable, "Assets/icons/diagnostic-error.svg",
                              round_to_int(close_cx), round_to_int(close_cy),
                              close_icon_sz, close_col, close_bg);
      }
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
    const int active_left =
        std::clamp(round_to_int(active_bounds.x), tab_bar_left, tab_bar_right);
    const int active_right = std::clamp(round_to_int(active_bounds.right()) - 1,
                                        tab_bar_left, tab_bar_right);

    if (active_left > tab_bar_left) {
      surface.draw_line(drawable, tab_bar_left, tab_bar_bottom, active_left,
                        tab_bar_bottom, surface.m_pixels.border);
    }
    if (active_right < tab_bar_right) {
      surface.draw_line(drawable, active_right, tab_bar_bottom, tab_bar_right,
                        tab_bar_bottom, surface.m_pixels.border);
    }
  } else {
    surface.draw_line(drawable, tab_bar_left, tab_bar_bottom, tab_bar_right,
                      tab_bar_bottom, surface.m_pixels.border);
  }

  surface.pop_clip();

  if (m_max_tab_scroll > 0.0F) {
    const float track_width = layout.tab_bar_bounds.width;
    const float thumb_width = std::max(
        20.0F * scale,
        track_width * (track_width / (track_width + m_max_tab_scroll)));
    const float thumb_x =
        layout.tab_bar_bounds.x +
        (m_tab_scroll_offset / m_max_tab_scroll) * (track_width - thumb_width);
    const UI::Rect thumb_bounds{thumb_x,
                                layout.tab_bar_bounds.bottom() - 3.0F * scale,
                                thumb_width, 3.0F * scale};
    const unsigned long thumb_color =
        m_hovered_tab_scrollbar
            ? surface.m_pixels.accent
            : surface.allocate_color(surface.m_palette.text_muted);
    surface.fill_rectangle(drawable, thumb_bounds, thumb_color);
  }
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const float scale = surface.m_dpi_scale;
  const auto *document = get_focused_document();

  if (document == nullptr) {
    // Empty state
    surface.fill_rectangle(drawable, layout.editor_bounds,
                           surface.m_pixels.editor_background);

    m_empty_state_open_btn.set_bounds(UI::Rect{});
    m_empty_state_clone_btn.set_bounds(UI::Rect{});

    const float dpi = surface.m_dpi_scale;
    const float logo_size = 180.0F * dpi;
    const float logo_gap = 32.0F * dpi;

    const std::string title = "Zenvra Development Studio 2026";
    const int title_w =
        surface.m_large_font
            ? surface.get_text_width(*surface.m_large_font, title)
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
    surface.draw_png_icon(drawable, "Assets/icons/zenvra_logo.png",
                          round_to_int(start_x + logo_size * 0.5F),
                          round_to_int(start_y + logo_size * 0.5F),
                          round_to_int(logo_size),
                          surface.m_palette.editor_background);

    const float text_x = start_x + logo_size + logo_gap;

    // 2. Heading "Zenvra Development Studio"
    if (surface.m_large_font) {
      surface.draw_text(drawable, *surface.m_large_font, title, text_x,
                        start_y + 36.0F * dpi, surface.m_text.primary);
    }

    // 3. Shortcuts list aligned directly under the heading (uniform neutral
    // tones, no blue)
    if (surface.m_small_font) {
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
        surface.draw_text(drawable, *surface.m_small_font, shortcuts[i].key,
                          text_x, row_y, surface.m_text.primary);
        surface.draw_text(drawable, *surface.m_small_font, shortcuts[i].label,
                          text_x + key_col_w, row_y, surface.m_text.muted);
      }
    }

    return;
  }

  // Draw single or split pane layout
  auto render_pane = [&](const UI::Editor::TextDocumentModel *doc,
                         const UI::Rect &gutter_rect, const UI::Rect &code_rect,
                         bool is_split_pane) {
    this->render_pane(surface, drawable, layout,
                      const_cast<UI::Editor::TextDocumentModel *>(doc),
                      gutter_rect, code_rect, is_split_pane);
  };

  if (m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size()) {
    const float split_x =
        layout.editor_bounds.x +
        (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
    const UI::Rect left_gutter = layout.gutter_bounds;
    const UI::Rect left_code{layout.editor_bounds.x, layout.editor_bounds.y,
                             split_x - layout.editor_bounds.x,
                             layout.editor_bounds.height};

    const float right_gutter_w = layout.gutter_bounds.width;
    const UI::Rect right_gutter{split_x + 2.0F * scale, layout.editor_bounds.y,
                                right_gutter_w, layout.editor_bounds.height};
    const UI::Rect right_code{right_gutter.right(), layout.editor_bounds.y,
                              layout.editor_bounds.right() -
                                  right_gutter.right(),
                              layout.editor_bounds.height};

    render_pane(m_controller.get_active_document(), left_gutter, left_code,
                false);
    render_pane(m_controller.get_document(*m_split_document_index),
                right_gutter, right_code, true);

    // Split close button in right pane top-right
    const float close_btn_size = 18.0F * scale;
    m_split_close_btn_bounds =
        UI::Rect{right_code.right() - close_btn_size - 8.0F * scale,
                 right_code.y + 6.0F * scale, close_btn_size, close_btn_size};
    surface.draw_svg_icon(
        drawable, "Assets/icons/diagnostic-error.svg",
        round_to_int(m_split_close_btn_bounds.x + close_btn_size * 0.5F),
        round_to_int(m_split_close_btn_bounds.y + close_btn_size * 0.5F),
        std::max(round_to_int(10.0F * scale), 8),
        m_hovered_split_close ? surface.m_palette.text_primary
                              : surface.m_palette.text_muted,
        surface.m_palette.editor_background);
  } else {
    render_pane(document, layout.gutter_bounds, layout.editor_bounds, false);
  }
}

void TextEditor::render_pane(const StudioWorkspaceRenderer &surface,
                             Drawable drawable,
                             const UI::Editor::StudioEditorLayoutResult &layout,
                             UI::Editor::TextDocumentModel *doc,
                             const UI::Rect &gutter_rect,
                             const UI::Rect &code_rect,
                             bool is_split_pane) const {
  if (!doc)
    return;

  const float scale = surface.m_dpi_scale;
  surface.fill_rectangle(drawable, gutter_rect,
                         surface.m_pixels.editor_background);
  surface.fill_rectangle(drawable, code_rect,
                         surface.m_pixels.editor_background);

  const float line_h = 20.0F * scale;
  const auto &folding = is_split_pane ? m_split_folding : m_folding;
  const std::size_t total_lines =
      std::max(doc->get_line_count(), std::size_t{1});
  const std::size_t vis_count = static_cast<std::size_t>(
      std::max(static_cast<int>(code_rect.height / line_h), 1));

  // Rebuild folding model only when document content actually changes (revision
  // cache)
  auto &last_doc = is_split_pane
                       ? const_cast<const UI::Editor::TextDocumentModel *&>(
                             m_split_last_folding_doc)
                       : const_cast<const UI::Editor::TextDocumentModel *&>(
                             m_last_folding_doc);
  auto &last_rev =
      is_split_pane ? const_cast<std::size_t &>(m_split_last_folding_revision)
                    : const_cast<std::size_t &>(m_last_folding_revision);
  if (last_doc != doc || last_rev != doc->get_revision()) {
    const_cast<UI::Components::EditorFoldingModel &>(folding).rebuild(
        doc->get_lines(), 4);
    last_doc = doc;
    last_rev = doc->get_revision();
  }

  auto &scrollbar = is_split_pane
                        ? const_cast<EditorScrollbar &>(m_split_scrollbar)
                        : const_cast<EditorScrollbar &>(m_scrollbar);
  scrollbar.synchronize(count_visible_lines(folding, total_lines), vis_count);

  const bool is_focused_pane =
      is_split_pane
          ? (m_focused_pane == SplitPaneFocus::Right)
          : (!m_is_split || m_focused_pane == SplitPaneFocus::Left);

  if (m_reveal_caret_pending && is_focused_pane) {
    static_cast<void>(scrollbar.reveal_line(physical_line_to_visual_row(
        folding, doc->get_caret_line(), total_lines)));
    const_cast<TextEditor *>(this)->m_reveal_caret_pending = false;
  }

  const std::size_t first_visual_row = scrollbar.get_first_visible_line();
  const std::size_t first_line =
      visual_row_to_physical_line(folding, first_visual_row, total_lines);
  const std::size_t render_count = vis_count;

  const float fold_margin =
      UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
  const float gutter_line_x = gutter_rect.right() - fold_margin - 1.0F;

  surface.draw_line(drawable, round_to_int(gutter_line_x),
                    round_to_int(gutter_rect.y), round_to_int(gutter_line_x),
                    round_to_int(gutter_rect.bottom()),
                    surface.m_pixels.border);

  const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
  const float minimap_w =
      (code_rect.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale)
          ? (FIXED_MINIMAP_WIDTH * scale)
          : 0.0F;
  const float code_limit =
      is_split_pane
          ? (code_rect.right() - scrollbar_w - minimap_w)
          : (layout.editor_bounds.right() - layout.scrollbar_bounds.width -
             layout.minimap_bounds.width);
  const UI::Rect code_clip{code_rect.x, code_rect.y,
                           std::max(code_limit - code_rect.x, 0.0F),
                           code_rect.height};

  const float code_x = code_rect.x + 14.0F * scale -
                       (is_split_pane ? 0.0F : m_text_scroll_offset);
  const bool is_pane_focused =
      is_split_pane ? (m_focused_pane == SplitPaneFocus::Right)
                    : (!m_is_split || m_focused_pane == SplitPaneFocus::Left);

  const bool syntax_highlighting =
      UI::Editor::supports_editor_syntax_highlighting(doc->get_file_name());

  const auto token_color =
      [&](UI::Editor::EditorTokenKind kind) -> const std::string & {
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

  // Indent guides (VS Code / Win32 style)
  const float space_width =
      static_cast<float>(surface.m_editor_font->getTextWidth(" "));
  const UI::Components::ActiveIndentScope active_scope =
      folding.get_active_indent_scope(doc->get_caret_line(), 4);

  std::size_t row_guide = 0;
  for (std::size_t line_index = first_line;
       row_guide < render_count && line_index < total_lines; ++line_index) {
    if (folding.is_line_hidden(line_index))
      continue;
    const float center_y =
        code_rect.y + line_h * 0.5F + static_cast<float>(row_guide) * line_h;
    const float y_top = center_y - line_h * 0.5F;
    const float y_bottom = center_y + line_h * 0.5F;
    ++row_guide;

    const std::size_t line_indent = folding.get_effective_indent(line_index);
    if (line_indent < 4)
      continue;

    for (std::size_t col = 4; col <= line_indent; col += 4) {
      const float guide_x = code_x + static_cast<float>(col) * space_width;
      if (guide_x < code_rect.x || guide_x > code_limit)
        continue;

      const bool is_active = active_scope.valid && col == active_scope.column &&
                             line_index >= active_scope.start_line &&
                             line_index <= active_scope.end_line;

      surface.draw_line(drawable, round_to_int(guide_x), round_to_int(y_top),
                        round_to_int(guide_x), round_to_int(y_bottom),
                        is_active ? surface.m_pixels.accent
                                  : surface.m_pixels.border);
    }
  }

  // Pass 1: Gutter background, line numbers, diagnostics dot, fold markers,
  // active line bg
  std::size_t row_pass1 = 0;
  for (std::size_t line_index = first_line;
       row_pass1 < render_count && line_index < total_lines; ++line_index) {
    if (folding.is_line_hidden(line_index))
      continue;

    const float line_y = code_rect.y + static_cast<float>(row_pass1) * line_h;
    const float center_y = line_y + line_h * 0.5F;
    ++row_pass1;

    const bool is_active_line =
        (line_index == doc->get_caret_line() && is_pane_focused);

    // Active line background across entire gutter and code pane (up to
    // code_limit)
    if (is_active_line && !doc->has_selection()) {
      surface.fill_rectangle(
          drawable,
          UI::Rect{gutter_rect.x, line_y, code_limit - gutter_rect.x, line_h},
          surface.m_pixels.active_line_background);
    }

    // Line number (right-aligned against gutter_line_x with 5px gap, matching
    // Win32)
    const std::string line_num = std::to_string(line_index + 1);
    const int num_w = surface.m_editor_font->getTextWidth(line_num);
    const float number_x =
        gutter_line_x - 5.0F * scale - static_cast<float>(num_w);
    surface.draw_text(
        drawable, *surface.m_editor_font, line_num, number_x, center_y,
        is_active_line ? surface.m_text.primary : surface.m_text.muted);

    // Diagnostics dot in gutter
    const auto diags = doc->get_diagnostics_for_line(line_index);
    if (!diags.empty()) {
      bool has_error = false;
      bool has_warn = false;
      for (const auto &gd : diags) {
        if (gd.severity == Language::Protocol::DiagnosticSeverity::Error)
          has_error = true;
        else if (gd.severity == Language::Protocol::DiagnosticSeverity::Warning)
          has_warn = true;
      }
      const float dot_x = gutter_rect.x + 4.0F * scale;
      const float dot_r = 3.0F * scale;
      const unsigned long dot_color =
          has_error
              ? surface.allocate_color(UI::Theme::Color{247, 84, 100, 255})
              : (has_warn ? surface.allocate_color(
                                UI::Theme::Color{240, 167, 50, 255})
                          : surface.allocate_color(
                                UI::Theme::Color{86, 182, 194, 255}));
      surface.fill_rounded_rectangle(
          drawable,
          UI::Rect{dot_x, center_y - dot_r, dot_r * 2.0F, dot_r * 2.0F},
          dot_color, dot_r);
    }

    // Fold markers
    const UI::Components::FoldMarker fold = folding.get_marker(line_index);
    const float fold_center_x = gutter_rect.right() - fold_margin * 0.5F;
    const int fold_cx = round_to_int(fold_center_x);
    const int fold_cy = round_to_int(center_y);
    const bool fold_hovered =
        m_hovered_fold_line && *m_hovered_fold_line == line_index;

    if (fold == UI::Components::FoldMarker::Expanded ||
        fold == UI::Components::FoldMarker::Collapsed) {
      const int box_half = std::max(round_to_int(4.5F * scale), 4);
      surface.fill_rectangle(drawable,
                             UI::Rect{static_cast<float>(fold_cx - box_half),
                                      static_cast<float>(fold_cy - box_half),
                                      static_cast<float>(box_half * 2),
                                      static_cast<float>(box_half * 2)},
                             is_active_line
                                 ? surface.m_pixels.active_line_background
                                 : surface.m_pixels.editor_background);
      surface.draw_rectangle(drawable,
                             UI::Rect{static_cast<float>(fold_cx - box_half),
                                      static_cast<float>(fold_cy - box_half),
                                      static_cast<float>(box_half * 2),
                                      static_cast<float>(box_half * 2)},
                             fold_hovered ? surface.m_pixels.accent
                                          : surface.m_pixels.border);
      const int sign_inset = std::max(round_to_int(2.0F * scale), 2);
      surface.draw_line(drawable, fold_cx - box_half + sign_inset, fold_cy,
                        fold_cx + box_half - sign_inset, fold_cy,
                        fold_hovered ? surface.m_pixels.accent
                                     : surface.m_pixels.text_muted);
      if (fold == UI::Components::FoldMarker::Collapsed) {
        surface.draw_line(drawable, fold_cx, fold_cy - box_half + sign_inset,
                          fold_cx, fold_cy + box_half - sign_inset,
                          fold_hovered ? surface.m_pixels.accent
                                       : surface.m_pixels.text_muted);
      }
    } else if (fold == UI::Components::FoldMarker::Continuation) {
      surface.draw_line(drawable, fold_cx, round_to_int(line_y), fold_cx,
                        round_to_int(line_y + line_h), surface.m_pixels.border);
    } else if (fold == UI::Components::FoldMarker::End) {
      surface.draw_line(drawable, fold_cx, round_to_int(line_y), fold_cx,
                        fold_cy, surface.m_pixels.border);
      surface.draw_line(drawable, fold_cx, fold_cy,
                        round_to_int(fold_center_x + 4.0F * scale), fold_cy,
                        surface.m_pixels.border);
    }
  }

  // Pass 1.5: Direct Crisp Selection (Instant, 0 Animation Delay)
  if (is_pane_focused) {
    for (const auto &cursor : doc->get_all_cursors()) {
      if (cursor.has_selection()) {
        const UI::Editor::TextSelection selection = cursor.get_selection();
        const std::size_t start_line =
            std::max(selection.start.line, first_line);
        const std::size_t end_line =
            std::min(selection.end.line, first_line + render_count);

        for (std::size_t line_index = start_line; line_index <= end_line;
             ++line_index) {
          if (folding.is_line_hidden(line_index) || line_index >= doc->get_line_count())
            continue;

          const std::string_view line = doc->get_line(line_index);
          const std::size_t selection_start =
              line_index == selection.start.line ? selection.start.column : 0;
          const std::size_t selection_end = line_index == selection.end.line
                                                ? selection.end.column
                                                : line.size();

          const float selection_x =
              static_cast<float>(surface.m_editor_font->getTextWidth(
                  line.substr(0, std::min(selection_start, line.size()))));
          float selection_width = static_cast<float>(
              surface.m_editor_font->getTextWidth(line.substr(
                  selection_start, (selection_end >= selection_start)
                                       ? (selection_end - selection_start)
                                       : 0)));

          if (line_index < selection.end.line) {
            selection_width += 6.0F * scale;
          }

          if (selection_width <= 0.0F)
            continue;

          const std::size_t visual_row =
              physical_line_to_visual_row(folding, line_index, total_lines);
          const float screen_y =
              code_rect.y + static_cast<float>(visual_row - first_visual_row) * line_h;
          const float screen_x = code_x + selection_x;

          if (screen_y + line_h >= code_rect.y &&
              screen_y <= code_rect.bottom()) {
            const int snap_y = round_to_int(screen_y);
            const int snap_bottom = round_to_int(screen_y + line_h);
            const int snap_x = round_to_int(screen_x);
            const int snap_right =
                round_to_int(std::min(screen_x + selection_width, code_limit));

            if (snap_right > snap_x) {
              surface.push_clip(code_clip);
              surface.fill_rounded_rectangle(
                  drawable,
                  UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y),
                           static_cast<float>(snap_right - snap_x),
                           static_cast<float>(snap_bottom - snap_y)},
                  surface.allocate_color(surface.m_palette.selection_background),
                  4.0F * scale, surface.m_pixels.editor_background);
              surface.pop_clip();
            }
          }
        }
      }
    }
  }

  // Pass 2: Text rendering with syntax highlighting, diagnostics squiggles, and
  // caret
  std::size_t row_pass2 = 0;
  float max_line_w = 0.0F;
  for (std::size_t line_index = first_line;
       row_pass2 < render_count && line_index < total_lines; ++line_index) {
    if (folding.is_line_hidden(line_index))
      continue;

    const std::string_view line = doc->get_line(line_index);
    const float line_y = code_rect.y + static_cast<float>(row_pass2) * line_h;
    const float center_y = line_y + line_h * 0.5F;
    ++row_pass2;

    const float line_width =
        static_cast<float>(surface.m_editor_font->getTextWidth(line));
    max_line_w = std::max(max_line_w, line_width);

    const auto line_diags = doc->get_diagnostics_for_line(line_index);
    auto get_effective_token_color = [&](UI::Editor::EditorTokenKind kind,
                                         std::size_t tok_start,
                                         std::size_t tok_len) -> std::string {
      const std::string &base_color = token_color(kind);
      bool is_unnecessary = false;
      for (const auto &d : line_diags) {
        if (d.is_unnecessary()) {
          const std::size_t d_start =
              (d.range.start.line == line_index) ? d.range.start.character : 0;
          const std::size_t d_end =
              (d.range.end.line == line_index)
                  ? (d.range.end.character == 0 ? line.size()
                                                : d.range.end.character)
                  : line.size();
          if (tok_start < d_end && (tok_start + tok_len) > d_start) {
            is_unnecessary = true;
            break;
          }
        }
      }
      if (is_unnecessary) {
        if (base_color.size() >= 7 && base_color[0] == '#') {
          unsigned int r = 0, g = 0, b = 0;
          if (std::sscanf(base_color.c_str() + 1, "%02x%02x%02x", &r, &g, &b) ==
              3) {
            // Softly dim the token color hue (55% base hue + 45% dark
            // background)
            const unsigned int dim_r = (r * 55 + 30 * 45) / 100;
            const unsigned int dim_g = (g * 55 + 31 * 45) / 100;
            const unsigned int dim_b = (b * 55 + 34 * 45) / 100;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", dim_r, dim_g,
                          dim_b);
            return std::string(buf);
          }
        }
      }
      return base_color;
    };

    // Text rendering with syntax highlighting and clip rect
    if (syntax_highlighting) {
      float token_x = code_x;
      std::size_t rendered_bytes = 0;
      std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>
          tokens{};
      auto line_state = doc->get_line_state(line_index);
      const std::size_t token_count = UI::Editor::tokenize_editor_line(
          line, tokens, doc->get_file_name(), &line_state);
      for (std::size_t token_index = 0; token_index < token_count;
           ++token_index) {
        const auto &tok = tokens[token_index];
        const std::size_t tok_len = tok.text.size();
        if (token_x < code_limit) {
          surface.draw_text(
              drawable, *surface.m_editor_font, tok.text, token_x, center_y,
              get_effective_token_color(tok.kind, rendered_bytes, tok_len),
              &code_clip);
        }
        token_x +=
            static_cast<float>(surface.m_editor_font->getTextWidth(tok.text));
        rendered_bytes += tok_len;
      }
    } else {
      bool is_unnecessary = false;
      for (const auto &d : line_diags) {
        if (d.is_unnecessary()) {
          is_unnecessary = true;
          break;
        }
      }
      surface.draw_text(
          drawable, *surface.m_editor_font, line, code_x, center_y,
          is_unnecessary ? "#808590" : surface.m_text.primary, &code_clip);
    }

    // Diagnostics squiggles under erroneous tokens (crisp sinusoidal wavy
    // squiggle matching VS Code)
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
          // If range starts before or at the quote, or spans only a single
          // symbol, span the header file path
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
        diag_start_x += static_cast<float>(
            surface.m_editor_font->getTextWidth(line.substr(0, start_col)));
      }
      float diag_width = 8.0F * scale;
      if (end_col > start_col && start_col < line.size()) {
        diag_width = static_cast<float>(surface.m_editor_font->getTextWidth(
            line.substr(start_col, end_col - start_col)));
      }

      const unsigned long squiggle_color =
          (diag.severity == Language::Protocol::DiagnosticSeverity::Error)
              ? surface.allocate_color(UI::Theme::Color{247, 84, 100, 255})
              : (diag.severity ==
                         Language::Protocol::DiagnosticSeverity::Warning
                     ? surface.allocate_color(
                           UI::Theme::Color{240, 167, 50, 255})
                     : surface.allocate_color(
                           UI::Theme::Color{86, 182, 194, 255}));

      // Draw smooth continuous sinusoidal wavy squiggle (VS Code & macOS Cocoa
      // style)
      const float wave_freq = 0.95F / std::max(scale, 0.5F);
      const float wave_amp = 1.35F * scale;
      const float wave_y = center_y + line_h * 0.44F;
      const float wave_step = 1.0F; // Smooth 1px incremental sampling
      const float wave_end_x = std::min(
          diag_start_x + std::max(diag_width, 6.0F * scale), code_limit);
      float prev_x = diag_start_x;
      float prev_y = wave_y;
      for (float curr_x = diag_start_x + wave_step; curr_x <= wave_end_x;
           curr_x += wave_step) {
        const float curr_y =
            wave_y + std::sin((curr_x - diag_start_x) * wave_freq) * wave_amp;
        surface.draw_line(drawable, round_to_int(prev_x), round_to_int(prev_y),
                          round_to_int(curr_x), round_to_int(curr_y),
                          squiggle_color);
        prev_x = curr_x;
        prev_y = curr_y;
      }
    }

    // Render Modern Flat Inline Diagnostic Lens (flex-centered matching Win32 /
    // VS Code)
    if (!line_diags.empty()) {
      const auto *top_diag = &line_diags[0];
      for (const auto &d : line_diags) {
        if (d.severity < top_diag->severity) {
          top_diag = &d;
        }
      }

      UI::Theme::Color badge_bg{44, 20, 26, 210};
      UI::Theme::Color badge_border{247, 84, 100, 80};
      UI::Theme::Color badge_fg{255, 120, 135, 255};
      UI::Theme::Color icon_color{247, 84, 100, 255};
      std::string icon_asset = "Assets/icons/diagnostic-error.svg";

      if (top_diag->severity ==
          Language::Protocol::DiagnosticSeverity::Warning) {
        badge_bg = UI::Theme::Color{44, 32, 14, 210};
        badge_border = UI::Theme::Color{240, 167, 50, 80};
        badge_fg = UI::Theme::Color{250, 188, 80, 255};
        icon_color = UI::Theme::Color{240, 167, 50, 255};
        icon_asset = "Assets/icons/diagnostic-warning.svg";
      } else if (top_diag->severity >=
                 Language::Protocol::DiagnosticSeverity::Information) {
        badge_bg = UI::Theme::Color{18, 34, 46, 210};
        badge_border = UI::Theme::Color{86, 182, 194, 80};
        badge_fg = UI::Theme::Color{105, 210, 225, 255};
        icon_color = UI::Theme::Color{86, 182, 194, 255};
        icon_asset = "Assets/icons/diagnostic-info.svg";
      }

      std::string msg = top_diag->message;
      for (char &ch : msg) {
        if (ch == '\r' || ch == '\n')
          ch = ' ';
      }

      const bool is_collapsed = (folding.get_marker(line_index) ==
                                 UI::Components::FoldMarker::Collapsed);
      const float lens_start_x =
          code_x + line_width + (is_collapsed ? 42.0F * scale : 20.0F * scale);
      const float avail_w =
          std::max(code_limit - lens_start_x - 16.0F * scale, 80.0F * scale);

      const float pad_x = 8.0F * scale;
      const float icon_size = 12.0F * scale;
      const float gap = 6.0F * scale;

      int msg_w = surface.m_ui_font->getTextWidth(msg);
      const float max_msg_w =
          std::max(avail_w - (pad_x * 2.0F + icon_size + gap), 40.0F * scale);
      if (static_cast<float>(msg_w) > max_msg_w && msg.size() > 8) {
        while (msg.size() > 4 &&
               static_cast<float>(
                   surface.m_ui_font->getTextWidth(msg + "...")) > max_msg_w) {
          msg.pop_back();
        }
        msg += "...";
        msg_w = surface.m_ui_font->getTextWidth(msg);
      }

      const float badge_w =
          pad_x + icon_size + gap + static_cast<float>(msg_w) + pad_x;
      const float badge_h = 18.0F * scale;
      const UI::Rect badge_rect{lens_start_x, center_y - badge_h * 0.5F,
                                badge_w, badge_h};

      if (badge_rect.x < code_limit) {
        // 1. Flat Pill Background + Subtle Border
        surface.fill_rounded_rectangle(drawable, badge_rect,
                                       surface.allocate_color(badge_bg),
                                       4.0F * scale);
        surface.draw_rounded_rectangle(drawable, badge_rect,
                                       surface.allocate_color(badge_border),
                                       4.0F * scale);

        // 2. Vector SVG Icon (flex-centered)
        const int icon_cx =
            round_to_int(lens_start_x + pad_x + icon_size * 0.5F);
        const int icon_cy = round_to_int(center_y);
        surface.draw_svg_icon(drawable, icon_asset, icon_cx, icon_cy,
                              std::max(round_to_int(icon_size), 10), icon_color,
                              badge_bg);

        // 3. Message Text (flex-centered vertically)
        const float text_x = lens_start_x + pad_x + icon_size + gap;
        surface.draw_text(drawable, *surface.m_ui_font, msg, text_x, center_y,
                          badge_fg, &code_clip);
      }
    }

    // Collapsed code placeholder badge (...)
    if (folding.get_marker(line_index) ==
        UI::Components::FoldMarker::Collapsed) {
      const float badge_x = code_x + line_width + 8.0F * scale;
      const float badge_w = 26.0F * scale;
      const float badge_h = 16.0F * scale;
      const UI::Rect badge_rect{badge_x, center_y - badge_h * 0.5F, badge_w,
                                badge_h};
      if (badge_rect.x < code_limit) {
        surface.fill_rounded_rectangle(
            drawable, badge_rect,
            surface.allocate_color(UI::Theme::Color{45, 50, 65, 230}),
            3.0F * scale);
        surface.draw_rounded_rectangle(
            drawable, badge_rect,
            surface.allocate_color(UI::Theme::Color{75, 84, 110, 255}),
            3.0F * scale);
        surface.draw_text(drawable, *surface.m_small_font, "...",
                          badge_x + 6.0F * scale, center_y,
                          UI::Theme::Color{210, 215, 230, 255}, &code_clip);
      }
    }

    // Caret rendering (multi-cursor support, white text_primary color matching
    // Win32)
    if (is_pane_focused && m_caret_blink.is_visible()) {
      for (const auto &cur : doc->get_all_cursors()) {
        if (cur.line == line_index) {
          const std::string_view caret_prefix =
              line.substr(0, std::min(cur.column, line.size()));
          const int caret_advance =
              surface.m_editor_font->getTextWidth(caret_prefix);
          const float caret_x = code_x + static_cast<float>(caret_advance);
          if (caret_x < code_limit) {
            surface.fill_rectangle(drawable,
                                   UI::Rect{caret_x, line_y + 2.0F * scale,
                                            2.0F * scale,
                                            line_h - 4.0F * scale},
                                   surface.m_pixels.text_primary);
          }
        }
      }
    }
  }

  if (!is_split_pane) {
    const_cast<TextEditor *>(this)->m_max_text_scroll =
        std::max(0.0F, max_line_w + 80.0F * scale - code_clip.width);
    if (m_text_scroll_offset > m_max_text_scroll) {
      const_cast<TextEditor *>(this)->m_text_scroll_offset = m_max_text_scroll;
    }
  }
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer &surface,
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) const {
  const float scale = surface.m_dpi_scale;
  const bool is_split_active =
      m_is_split && m_split_document_index.has_value() &&
      *m_split_document_index < m_controller.get_documents().size();
  const float splitter_x =
      layout.editor_bounds.x +
      (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

  if (is_split_active && m_focused_pane == SplitPaneFocus::Right) {
    const UI::Editor::TextDocumentModel *split_doc =
        m_controller.get_document(*m_split_document_index);
    if (split_doc == nullptr)
      return {};

    const float line_height = 20.0F * scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines =
        std::max(split_doc->get_line_count(), std::size_t{1});
    m_split_scrollbar.synchronize(
        count_visible_lines(m_split_folding, total_lines), visible_count);

    const std::size_t first_line = m_split_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        point_y, layout.editor_bounds.y,
        std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height),
        0));
    const std::size_t line_index = visual_row_to_physical_line(
        m_split_folding, first_line + clicked_row, total_lines);
    const std::string_view line = (line_index < split_doc->get_line_count())
                                      ? split_doc->get_line(line_index)
                                      : std::string_view{};

    const float right_x = splitter_x + 2.0F * scale;
    const float right_gutter_w = layout.gutter_bounds.width;
    const float code_x = right_x + right_gutter_w + 14.0F * scale;
    const float target_x = std::max(point_x - code_x, 0.0F);

    const float char_w = std::max(
        static_cast<float>(surface.m_editor_font->getTextWidth(" ")), 1.0F);
    const float tab_w = char_w * 4.0F;

    // Monospace fast path: if no tabs in the line, compute column via division
    if (line.find('\t') == std::string_view::npos) {
      const std::size_t col = static_cast<std::size_t>(
          std::max(static_cast<int>((target_x + char_w * 0.5F) / char_w), 0));
      return {line_index, std::min(col, line.size())};
    }

    std::size_t column = 0;
    float current_x = 0.0F;
    while (column < line.size()) {
      const std::size_t next_col = next_character_column(line, column);
      const float glyph_w = (line[column] == '\t') ? tab_w : char_w;
      if (target_x < current_x + glyph_w * 0.5F) {
        break;
      }
      current_x += glyph_w;
      column = next_col;
    }
    return {line_index, column};
  }

  const UI::Editor::TextDocumentModel *document =
      m_controller.get_active_document();
  if (document == nullptr) {
    return {};
  }
  const float line_height = 20.0F * surface.m_dpi_scale;
  const std::size_t visible_count = static_cast<std::size_t>(
      std::max(static_cast<int>(layout.editor_bounds.height / line_height), 1));
  const std::size_t total_lines =
      std::max(document->get_line_count(), std::size_t{1});
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
  const std::string_view line = (line_index < document->get_line_count())
                                    ? document->get_line(line_index)
                                    : std::string_view{};
  const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale -
                       m_text_scroll_offset;
  const float target_x = std::max(point_x - code_x, 0.0F);

  const float char_w = std::max(
      static_cast<float>(surface.m_editor_font->getTextWidth(" ")), 1.0F);
  const float tab_w = char_w * 4.0F;

  // Monospace fast path: if no tabs in the line, column = floor(target_x /
  // char_w + 0.5) This avoids calling getTextWidth per-character in the hot
  // drag path
  if (line.find('\t') == std::string_view::npos) {
    const std::size_t col = static_cast<std::size_t>(
        std::max(static_cast<int>((target_x + char_w * 0.5F) / char_w), 0));
    return {line_index, std::min(col, line.size())};
  }

  std::size_t column = 0;
  float current_x = 0.0F;
  while (column < line.size()) {
    const std::size_t next_col = next_character_column(line, column);
    const float glyph_w = (line[column] == '\t') ? tab_w : char_w;
    if (target_x < current_x + glyph_w * 0.5F) {
      break;
    }
    current_x += glyph_w;
    column = next_col;
  }
  return {line_index, column};
}

void TextEditor::draw_diagnostic_hover_overlay(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  if (!m_hovered_diagnostic.has_value()) {
    return;
  }

  const auto &info = *m_hovered_diagnostic;
  const auto &diag = info.diagnostic;
  const float scale = surface.m_dpi_scale;

  UI::Theme::Color badge_bg{70, 24, 30, 255};
  UI::Theme::Color badge_border{247, 84, 100, 255};
  UI::Theme::Color badge_fg{255, 130, 145, 255};
  UI::Theme::Color icon_color{247, 84, 100, 255};
  std::string severity_text = "Error";
  std::string icon_asset = "Assets/icons/diagnostic-error.svg";

  if (diag.severity == Language::Protocol::DiagnosticSeverity::Warning) {
    badge_bg = UI::Theme::Color{70, 48, 20, 255};
    badge_border = UI::Theme::Color{240, 167, 50, 255};
    badge_fg = UI::Theme::Color{255, 195, 80, 255};
    icon_color = UI::Theme::Color{240, 167, 50, 255};
    severity_text = "Warning";
    icon_asset = "Assets/icons/diagnostic-warning.svg";
  } else if (diag.severity >=
             Language::Protocol::DiagnosticSeverity::Information) {
    badge_bg = UI::Theme::Color{24, 48, 64, 255};
    badge_border = UI::Theme::Color{86, 182, 194, 255};
    badge_fg = UI::Theme::Color{120, 220, 235, 255};
    icon_color = UI::Theme::Color{86, 182, 194, 255};
    severity_text = "Info";
    icon_asset = "Assets/icons/diagnostic-info.svg";
  }

  std::string msg = diag.message;
  for (char &ch : msg) {
    if (ch == '\r' || ch == '\n')
      ch = ' ';
  }

  std::string source_tag = diag.source.empty() ? "clangd" : diag.source;
  if (!diag.code.empty()) {
    source_tag += " (" + diag.code + ")";
  }

  const int msg_w = surface.m_ui_font->getTextWidth(msg);
  const int src_w = surface.m_small_font
                        ? surface.m_small_font->getTextWidth(source_tag)
                        : 40;
  const int sev_w = surface.m_small_font
                        ? surface.m_small_font->getTextWidth(severity_text)
                        : 45;

  const float card_padding = 14.0F * scale;
  const float max_w = std::max(
      120.0F * scale,
      std::min(layout.editor_bounds.width - 40.0F * scale, 640.0F * scale));
  const float min_w = std::min(380.0F * scale, max_w);
  const float card_w = std::clamp(static_cast<float>(msg_w) +
                                      card_padding * 2.0F + 30.0F * scale,
                                  min_w, max_w);
  const float card_h = 76.0F * scale;
  const float card_radius = 8.0F * scale;

  float card_y = info.anchor_y - card_h - 6.0F * scale;
  if (card_y < layout.editor_bounds.y + 4.0F * scale) {
    card_y = info.anchor_y + 24.0F * scale;
  }
  const float min_card_x = layout.editor_bounds.x + 8.0F * scale;
  const float max_card_x = std::max(min_card_x, layout.editor_bounds.right() -
                                                    card_w - 8.0F * scale);
  const float card_x =
      std::clamp(info.anchor_x - 30.0F * scale, min_card_x, max_card_x);

  const UI::Rect card_rect{card_x, card_y, card_w, card_h};

  // 1. Soft macOS Drop Shadow Simulation
  const unsigned long shadow_col1 =
      surface.allocate_color(UI::Theme::Color{0, 0, 0, 80});
  const unsigned long shadow_col2 =
      surface.allocate_color(UI::Theme::Color{0, 0, 0, 40});
  surface.fill_rounded_rectangle(
      drawable,
      UI::Rect{card_x - 2.0F * scale, card_y + 3.0F * scale,
               card_w + 4.0F * scale, card_h + 4.0F * scale},
      shadow_col2, card_radius + 2.0F * scale,
      surface.m_pixels.editor_background);
  surface.fill_rounded_rectangle(
      drawable,
      UI::Rect{card_x - 1.0F * scale, card_y + 1.5F * scale,
               card_w + 2.0F * scale, card_h + 2.0F * scale},
      shadow_col1, card_radius + 1.0F * scale,
      surface.m_pixels.editor_background);

  // 2. Dark Acrylic Card Background (30, 31, 34) & 1px Border (58, 62, 72)
  const unsigned long card_bg =
      surface.allocate_color(UI::Theme::Color{30, 31, 34, 255});
  const unsigned long card_border =
      surface.allocate_color(UI::Theme::Color{58, 62, 72, 255});
  surface.fill_rounded_rectangle(drawable, card_rect, card_bg, card_radius,
                                 surface.m_pixels.editor_background);
  surface.draw_rounded_rectangle(drawable, card_rect, card_border, card_radius);

  // 3. Header Badges: SVG Icon + Severity Text Pill + Source Tag Pill
  const float header_y = card_y + 8.0F * scale;
  const float badge_h = 18.0F * scale;
  const float icon_size = 11.0F * scale;
  const float sev_pill_w =
      icon_size + 6.0F * scale + static_cast<float>(sev_w) + 12.0F * scale;
  const UI::Rect sev_rect{card_x + card_padding, header_y, sev_pill_w, badge_h};
  surface.fill_rounded_rectangle(drawable, sev_rect,
                                 surface.allocate_color(badge_bg), 4.0F * scale,
                                 card_bg);
  surface.draw_rounded_rectangle(
      drawable, sev_rect, surface.allocate_color(badge_border), 4.0F * scale);

  // Draw SVG vector icon inside severity pill
  const int icon_cx =
      round_to_int(card_x + card_padding + 6.0F * scale + icon_size * 0.5F);
  const int icon_cy = round_to_int(header_y + badge_h * 0.5F);
  surface.draw_svg_icon(drawable, icon_asset, icon_cx, icon_cy,
                        std::max(round_to_int(icon_size), 10), icon_color,
                        badge_bg);

  // Draw severity label text next to SVG icon
  if (surface.m_small_font) {
    surface.draw_text(drawable, *surface.m_small_font, severity_text,
                      card_x + card_padding + icon_size + 9.0F * scale,
                      header_y + badge_h * 0.5F, badge_fg);
  }

  // Source Tag Pill (e.g. "clangd")
  const float src_pill_x = card_x + card_padding + sev_pill_w + 6.0F * scale;
  const float src_pill_w = static_cast<float>(src_w) + 10.0F * scale;
  const UI::Rect src_rect{src_pill_x, header_y, src_pill_w, badge_h};
  surface.fill_rounded_rectangle(
      drawable, src_rect,
      surface.allocate_color(UI::Theme::Color{38, 42, 52, 255}), 4.0F * scale,
      card_bg);
  if (surface.m_small_font) {
    surface.draw_text(drawable, *surface.m_small_font, source_tag,
                      src_pill_x + 5.0F * scale, header_y + badge_h * 0.5F,
                      UI::Theme::Color{156, 220, 254, 255});
  }

  // 4. Diagnostic Message Text
  const float msg_y = card_y + 36.0F * scale;
  surface.draw_text(drawable, *surface.m_ui_font, msg, card_x + card_padding,
                    msg_y, UI::Theme::Color{240, 240, 245, 255}, &card_rect);

  // 5. Quick Fix Guidance Hint at Bottom
  const float hint_y = card_y + 58.0F * scale;
  if (surface.m_small_font) {
    surface.draw_text(
        drawable, *surface.m_small_font, "💡 Quick Fix available (Alt+Enter)",
        card_x + card_padding, hint_y, UI::Theme::Color{83, 132, 228, 255});
  }
}

void TextEditor::draw_completion_popup(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
  const auto *doc = get_focused_document();
  if (!m_completion_popup.is_visible() ||
      m_completion_popup.get_item_count() == 0 || doc == nullptr) {
    return;
  }

  const float scale = surface.m_dpi_scale;
  const float line_h = 20.0F * scale;
  const std::string_view current_line = doc->get_line(doc->get_caret_line());
  const std::string_view prefix = current_line.substr(
      0, std::min(doc->get_caret_column(), current_line.size()));

  const float code_x =
      layout.editor_bounds.x + 14.0F * scale - m_text_scroll_offset;
  const float caret_screen_x =
      code_x + static_cast<float>(surface.m_editor_font->getTextWidth(prefix));
  const float caret_line_y =
      layout.editor_bounds.y +
      static_cast<float>(physical_line_to_visual_row(
                             m_folding, doc->get_caret_line(),
                             std::max(doc->get_line_count(), std::size_t{1})) -
                         m_scrollbar.get_first_visible_line() + 1) *
          line_h;

  const float item_h = 24.0F * scale;
  const float footer_h = 26.0F * scale;
  const std::size_t count = m_completion_popup.get_item_count();
  const std::size_t scroll_offset = m_completion_popup.get_scroll_offset();
  const std::size_t max_visible =
      std::min<std::size_t>(m_completion_popup.get_max_visible_items(), 12);
  const std::size_t visible_count = std::min<std::size_t>(count, max_visible);
  const float popup_h =
      static_cast<float>(visible_count) * item_h + footer_h + 10.0F * scale;

  float max_label_w = 280.0F * scale;
  for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count; ++i) {
    if (const auto *it = m_completion_popup.get_item(scroll_offset + i)) {
      const int w = surface.m_editor_font->getTextWidth(it->label);
      max_label_w =
          std::max(max_label_w, static_cast<float>(w) + 80.0F * scale);
    }
  }
  const float popup_w = std::clamp(max_label_w, 380.0F * scale, 540.0F * scale);

  const float popup_x = std::clamp(
      caret_screen_x, layout.editor_bounds.x + 8.0F * scale,
      std::max(layout.editor_bounds.x + 8.0F * scale,
               layout.editor_bounds.right() - (popup_w + 12.0F * scale)));
  float popup_y = caret_line_y;
  if (popup_y + popup_h > layout.editor_bounds.bottom() - 10.0F * scale) {
    popup_y = std::max(layout.editor_bounds.y + 4.0F * scale,
                       caret_line_y - line_h - popup_h);
  }

  const UI::Rect actual_bounds{popup_x, popup_y, popup_w, popup_h};
  const float card_radius = 10.0F * scale;

  // 1. Native macOS Drop Shadow Simulation (4 soft outer rings)
  const unsigned long shadow_col1 =
      surface.allocate_color(UI::Theme::Color{0, 0, 0, 90});
  const unsigned long shadow_col2 =
      surface.allocate_color(UI::Theme::Color{0, 0, 0, 50});
  const unsigned long shadow_col3 =
      surface.allocate_color(UI::Theme::Color{0, 0, 0, 25});
  surface.fill_rounded_rectangle(drawable,
                                 UI::Rect{actual_bounds.x - 3.0F * scale,
                                          actual_bounds.y + 3.0F * scale,
                                          actual_bounds.width + 6.0F * scale,
                                          actual_bounds.height + 6.0F * scale},
                                 shadow_col3, card_radius + 3.0F * scale,
                                 surface.m_pixels.editor_background);
  surface.fill_rounded_rectangle(drawable,
                                 UI::Rect{actual_bounds.x - 2.0F * scale,
                                          actual_bounds.y + 2.0F * scale,
                                          actual_bounds.width + 4.0F * scale,
                                          actual_bounds.height + 4.0F * scale},
                                 shadow_col2, card_radius + 2.0F * scale,
                                 surface.m_pixels.editor_background);
  surface.fill_rounded_rectangle(drawable,
                                 UI::Rect{actual_bounds.x - 1.0F * scale,
                                          actual_bounds.y + 1.0F * scale,
                                          actual_bounds.width + 2.0F * scale,
                                          actual_bounds.height + 2.0F * scale},
                                 shadow_col1, card_radius + 1.0F * scale,
                                 surface.m_pixels.editor_background);

  // 2. Native macOS Dark Card Fill (30, 31, 34)
  const unsigned long card_bg =
      surface.allocate_color(UI::Theme::Color{30, 31, 34, 255});
  surface.fill_rounded_rectangle(drawable, actual_bounds, card_bg, card_radius,
                                 surface.m_pixels.editor_background);

  // 3. Native macOS 1px Rounded Outer Border (58, 60, 68)
  const unsigned long card_border =
      surface.allocate_color(UI::Theme::Color{58, 60, 68, 255});
  surface.draw_rounded_rectangle(drawable, actual_bounds, card_border,
                                 card_radius);

  const std::size_t selected = m_completion_popup.get_selected_index();
  const std::string &query = m_completion_popup.get_filter();

  // 4. Item Rows with Rounded 6.0px Selection Pills
  for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count; ++i) {
    const std::size_t item_idx = scroll_offset + i;
    const auto *item = m_completion_popup.get_item(item_idx);
    if (item == nullptr)
      continue;

    const float row_y =
        actual_bounds.y + 6.0F * scale + static_cast<float>(i) * item_h;
    const float item_w =
        actual_bounds.width - (count > max_visible ? 14.0F : 10.0F) * scale;
    const UI::Rect item_rect{actual_bounds.x + 5.0F * scale, row_y, item_w,
                             item_h};

    if (item_idx == selected) {
      const unsigned long sel_bg =
          surface.allocate_color(UI::Theme::Color{50, 56, 68, 242});
      surface.fill_rounded_rectangle(drawable, item_rect, sel_bg, 6.0F * scale,
                                     card_bg);
    }

    // Minimalist Syntax Color matching macOS Cocoa
    std::string label_color = "#4fc1ff"; // Bright keyword/default blue
    if (item->kind == Language::Protocol::CompletionItemKind::Function ||
        item->kind == Language::Protocol::CompletionItemKind::Method) {
      label_color = "#dcdcaa";
    } else if (item->kind == Language::Protocol::CompletionItemKind::Variable ||
               item->kind == Language::Protocol::CompletionItemKind::Field) {
      label_color = "#9cdcfe";
    } else if (item->kind == Language::Protocol::CompletionItemKind::Class ||
               item->kind == Language::Protocol::CompletionItemKind::Struct ||
               item->kind ==
                   Language::Protocol::CompletionItemKind::Interface) {
      label_color = "#4ec9b0";
    } else if (item->kind == Language::Protocol::CompletionItemKind::Snippet) {
      label_color = "#f59e0b";
    }

    const float text_x = item_rect.x + 12.0F * scale;
    surface.draw_text(drawable, *surface.m_editor_font, item->label, text_x,
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
        const std::string matched_sub = item->label.substr(m_pos, query.size());
        surface.draw_text(drawable, *surface.m_editor_font, matched_sub,
                          text_x + static_cast<float>(w_before),
                          row_y + item_h * 0.5F, "#ffffff", &item_rect);
      }
    }

    // Right side detail / type hint
    if (!item->detail.empty()) {
      auto &font =
          surface.m_small_font ? *surface.m_small_font : *surface.m_ui_font;
      const int detail_w = font.getTextWidth(item->detail);
      const float detail_x = std::max(
          item_rect.x + 220.0F * scale,
          item_rect.right() - static_cast<float>(detail_w) - 10.0F * scale);
      surface.draw_text(drawable, font, item->detail, detail_x,
                        row_y + item_h * 0.5F, "#888890", &item_rect);
    }
  }

  // 5. Minimalist Scrollbar Pill
  if (count > max_visible) {
    const float track_x = actual_bounds.right() - 5.0F * scale;
    const float track_y = actual_bounds.y + 6.0F * scale;
    const float track_h = static_cast<float>(visible_count) * item_h;
    const float thumb_h =
        std::max(14.0F * scale, track_h * (static_cast<float>(max_visible) /
                                           static_cast<float>(count)));
    const float max_scroll = static_cast<float>(count - max_visible);
    const float thumb_y =
        track_y +
        (static_cast<float>(scroll_offset) / max_scroll) * (track_h - thumb_h);

    const UI::Rect thumb_rect{track_x, thumb_y, 3.0F * scale, thumb_h};
    const unsigned long thumb_bg =
        surface.allocate_color(UI::Theme::Color{110, 110, 110, 150});
    surface.fill_rounded_rectangle(drawable, thumb_rect, thumb_bg, 1.5F * scale,
                                   card_bg);
  }

  // 6. Bottom Modal Footer (Tips & Help matching macOS Cocoa)
  const float footer_y = actual_bounds.bottom() - footer_h;
  const unsigned long footer_line_col =
      surface.allocate_color(UI::Theme::Color{48, 50, 55, 255});
  surface.draw_line(drawable, round_to_int(actual_bounds.x),
                    round_to_int(footer_y), round_to_int(actual_bounds.right()),
                    round_to_int(footer_y), footer_line_col);

  auto &tip_font =
      surface.m_small_font ? *surface.m_small_font : *surface.m_ui_font;
  const std::string tip_text =
      "Press Tab or ↵ to choose the selected suggestion";
  surface.draw_text(drawable, tip_font, tip_text,
                    actual_bounds.x + 12.0F * scale, footer_y + footer_h * 0.5F,
                    "#888890");

  const int tip_w = tip_font.getTextWidth(tip_text);
  const float next_tip_x =
      actual_bounds.x + 18.0F * scale + static_cast<float>(tip_w);
  if (next_tip_x + 60.0F * scale < actual_bounds.right() - 24.0F * scale) {
    surface.draw_text(drawable, tip_font, "Next Tip", next_tip_x,
                      footer_y + footer_h * 0.5F, "#5384e4");
  }

  // Vertical 3-dots ⋮ on right
  surface.draw_text(drawable, tip_font, "⋮",
                    actual_bounds.right() - 16.0F * scale,
                    footer_y + footer_h * 0.5F, "#888890");

  // 7. Render Detail / Documentation Flyout Popup Card (macOS Cocoa Floating
  // Card Style)
  const auto *selected_item = m_completion_popup.get_selected_item();
  if (selected_item != nullptr &&
      (!selected_item->detail.empty() ||
       !selected_item->documentation.empty() ||
       selected_item->kind != Language::Protocol::CompletionItemKind::Text)) {
    const float detail_pad = 12.0F * scale;
    const float detail_w = 340.0F * scale;

    // Check if there is enough space on the right, otherwise place on the left
    float detail_x = actual_bounds.right() + 6.0F * scale;
    if (detail_x + detail_w > layout.editor_bounds.right() - 8.0F * scale) {
      detail_x = actual_bounds.x - detail_w - 6.0F * scale;
      if (detail_x < layout.editor_bounds.x + 8.0F * scale) {
        detail_x =
            std::max(layout.editor_bounds.x + 8.0F * scale,
                     layout.editor_bounds.right() - detail_w - 8.0F * scale);
      }
    }

    // Word wrap documentation lines
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
                  (detail_w - detail_pad * 2.0F - 4.0F * scale) &&
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

    const float header_h = 28.0F * scale;
    const float line_spacing = 18.0F * scale;
    const float content_h =
        header_h + (doc_lines.empty()
                        ? 8.0F * scale
                        : (8.0F * scale +
                           static_cast<float>(doc_lines.size()) * line_spacing +
                           detail_pad));
    const float detail_h = std::clamp(std::max(actual_bounds.height, content_h),
                                      56.0F * scale, 320.0F * scale);
    const float detail_y = actual_bounds.y;

    const UI::Rect detail_bounds{detail_x, detail_y, detail_w, detail_h};

    // Soft Shadow for Detail Flyout
    surface.fill_rounded_rectangle(
        drawable,
        UI::Rect{detail_bounds.x - 2.0F * scale, detail_bounds.y + 2.0F * scale,
                 detail_bounds.width + 4.0F * scale,
                 detail_bounds.height + 4.0F * scale},
        shadow_col2, card_radius + 2.0F * scale,
        surface.m_pixels.editor_background);

    // Background & Border
    surface.fill_rounded_rectangle(drawable, detail_bounds, card_bg,
                                   card_radius,
                                   surface.m_pixels.editor_background);
    surface.draw_rounded_rectangle(drawable, detail_bounds, card_border,
                                   card_radius);

    // Top Header: Kind Badge + Label + Signature/Detail
    float cursor_x = detail_bounds.x + detail_pad;
    const float cursor_y = detail_bounds.y + header_h * 0.5F;

    std::string kind_name;
    std::string kind_badge = " ";
    UI::Theme::Color badge_color = surface.m_palette.accent;

    switch (selected_item->kind) {
    case Language::Protocol::CompletionItemKind::Snippet:
      kind_badge = "[]";
      badge_color = UI::Theme::Color{79, 193, 255, 255};
      kind_name = "(snippet)";
      break;
    case Language::Protocol::CompletionItemKind::Keyword:
      kind_badge = "{}";
      badge_color = UI::Theme::Color{197, 134, 192, 255};
      kind_name = "(keyword)";
      break;
    case Language::Protocol::CompletionItemKind::Function:
    case Language::Protocol::CompletionItemKind::Method:
      kind_badge = "f";
      badge_color = UI::Theme::Color{177, 128, 215, 255};
      kind_name = "(function)";
      break;
    case Language::Protocol::CompletionItemKind::Variable:
    case Language::Protocol::CompletionItemKind::Field:
      kind_badge = "v";
      badge_color = UI::Theme::Color{156, 220, 254, 255};
      kind_name = "(variable)";
      break;
    case Language::Protocol::CompletionItemKind::Property:
      kind_badge = "p";
      badge_color = UI::Theme::Color{79, 193, 255, 255};
      kind_name = "(property)";
      break;
    case Language::Protocol::CompletionItemKind::Class:
    case Language::Protocol::CompletionItemKind::Struct:
    case Language::Protocol::CompletionItemKind::Interface:
      kind_badge = "c";
      badge_color = UI::Theme::Color{78, 201, 176, 255};
      kind_name = "(type)";
      break;
    case Language::Protocol::CompletionItemKind::File:
      kind_badge = "h";
      badge_color = UI::Theme::Color{156, 220, 254, 255};
      kind_name = "(header)";
      break;
    case Language::Protocol::CompletionItemKind::Module:
      kind_badge = "m";
      badge_color = UI::Theme::Color{220, 220, 170, 255};
      kind_name = "(module)";
      break;
    default:
      kind_badge = "abc";
      badge_color = surface.m_palette.text_muted;
      kind_name = "";
      break;
    }

    surface.draw_text(drawable, *surface.m_ui_font, kind_badge, cursor_x,
                      cursor_y, badge_color);
    cursor_x += 20.0F * scale;

    std::string header_text = selected_item->detail.empty()
                                  ? (kind_name + " " + selected_item->label)
                                  : selected_item->detail;
    surface.draw_text(drawable, *surface.m_editor_font, header_text, cursor_x,
                      cursor_y, UI::Theme::Color{230, 230, 235, 255});

    // Divider line
    const float sep_y = detail_bounds.y + header_h;
    surface.draw_line(drawable, round_to_int(detail_bounds.x),
                      round_to_int(sep_y), round_to_int(detail_bounds.right()),
                      round_to_int(sep_y), footer_line_col);

    // Documentation text lines
    float doc_y = sep_y + 12.0F * scale;
    bool inside_code_block = false;
    for (const auto &doc_line : doc_lines) {
      if (doc_y + line_spacing * 0.5F > detail_bounds.bottom() - 4.0F * scale)
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
          surface.draw_text(drawable, *surface.m_editor_font, doc_line,
                            detail_bounds.x + detail_pad + 6.0F * scale, doc_y,
                            UI::Theme::Color{230, 230, 240, 255});
        } else if (doc_line.starts_with("- ")) {
          surface.draw_text(drawable, *surface.m_ui_font,
                            "• " + doc_line.substr(2),
                            detail_bounds.x + detail_pad + 4.0F * scale, doc_y,
                            UI::Theme::Color{210, 210, 215, 255});
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

void TextEditor::draw_signature_help(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  std::lock_guard<std::recursive_mutex> lock(m_lsp_mutex);
  const auto *doc = get_focused_document();
  if (!m_signature_help.is_visible() ||
      m_signature_help.get_help().signatures.empty() || doc == nullptr) {
    return;
  }

  const float scale = surface.m_dpi_scale;
  const float line_h = 20.0F * scale;
  const std::string_view current_line = doc->get_line(doc->get_caret_line());
  const std::size_t caret_col = doc->get_caret_column();
  const std::string_view prefix =
      current_line.substr(0, std::min(caret_col, current_line.size()));

  const auto &sig = m_signature_help.get_help().signatures[0];
  const float code_x =
      layout.editor_bounds.x + 14.0F * scale - m_text_scroll_offset;
  const float caret_screen_x =
      code_x + static_cast<float>(surface.m_editor_font->getTextWidth(prefix));
  const float line_top_y =
      layout.editor_bounds.y +
      static_cast<float>(physical_line_to_visual_row(
                             m_folding, doc->get_caret_line(),
                             std::max(doc->get_line_count(), std::size_t{1})) -
                         m_scrollbar.get_first_visible_line()) *
          line_h;

  const int text_w = surface.m_editor_font->getTextWidth(sig.label);
  const float hint_w = static_cast<float>(text_w) + 16.0F * scale;
  const float hint_h = 22.0F * scale;
  const float hint_x = std::clamp(
      caret_screen_x, layout.editor_bounds.x + 10.0F * scale,
      std::max(layout.editor_bounds.x + 10.0F * scale,
               layout.editor_bounds.right() - (hint_w + 20.0F * scale)));

  float hint_y = line_top_y - hint_h - 3.0F * scale;
  if (hint_y < layout.editor_bounds.y + 2.0F * scale) {
    hint_y = line_top_y + line_h + 2.0F * scale;
  }

  const UI::Rect hint_bounds{hint_x, hint_y, hint_w, hint_h};

  const unsigned long hint_bg =
      surface.allocate_color(UI::Theme::Color{24, 24, 30, 255});
  const unsigned long hint_border =
      surface.allocate_color(UI::Theme::Color{55, 55, 68, 255});

  surface.fill_rounded_rectangle(drawable, hint_bounds, hint_bg, 3.0F * scale,
                                 surface.m_pixels.editor_background);
  surface.draw_rectangle(drawable, hint_bounds, hint_border);

  surface.draw_text(drawable, *surface.m_editor_font, sig.label,
                    hint_bounds.x + 8.0F * scale, hint_bounds.y + hint_h * 0.5F,
                    UI::Theme::Color{220, 220, 230, 255});
}

void TextEditor::draw_hover_tooltip(
    const StudioWorkspaceRenderer &surface, Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  if (!m_hover_tooltip.is_visible())
    return;

  const float scale = surface.m_dpi_scale;
  const auto &text = m_hover_tooltip.get_content();
  if (text.empty())
    return;

  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  int max_w = 0;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line.starts_with("```") || line == "---" || line == "***" || line == "___")
      continue;
    const int w = surface.m_ui_font->getTextWidth(line);
    if (w > max_w)
      max_w = w;
    lines.push_back(line);
  }
  if (lines.empty())
    return;

  const float line_h = 18.0F * scale;
  const float tooltip_w = static_cast<float>(max_w) + 20.0F * scale;
  const float tooltip_h =
      static_cast<float>(lines.size()) * line_h + 10.0F * scale;

  float tip_x = m_hover_tooltip.get_x();
  float tip_y = m_hover_tooltip.get_y();

  if (tip_x + tooltip_w > layout.editor_bounds.right() - 10.0F * scale) {
    tip_x = std::max(layout.editor_bounds.x + 10.0F * scale,
                     layout.editor_bounds.right() - tooltip_w - 10.0F * scale);
  }
  if (tip_y + tooltip_h > layout.editor_bounds.bottom() - 10.0F * scale) {
    tip_y = std::max(layout.editor_bounds.y + 10.0F * scale,
                     tip_y - tooltip_h - 20.0F * scale);
  }

  const UI::Rect bounds{tip_x, tip_y, tooltip_w, tooltip_h};
  const unsigned long bg =
      surface.allocate_color(UI::Theme::Color{30, 30, 36, 255});
  const unsigned long border =
      surface.allocate_color(UI::Theme::Color{60, 60, 70, 255});

  surface.fill_rounded_rectangle(drawable, bounds, bg, 4.0F * scale,
                                 surface.m_pixels.editor_background);
  surface.draw_rectangle(drawable, bounds, border);

  float current_y = bounds.y + 6.0F * scale;
  for (const auto &l : lines) {
    surface.draw_text(drawable, *surface.m_ui_font, l, bounds.x + 10.0F * scale,
                      current_y + line_h * 0.5F,
                      UI::Theme::Color{230, 230, 240, 255});
    current_y += line_h;
  }
}

} // namespace Zenvra::Platform::X11::Components
