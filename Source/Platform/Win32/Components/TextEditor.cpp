#include "Platform/Win32/Components/TextEditor.h"

#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

namespace
{

using Zenvra::Utility::round_to_int;

constexpr float FIXED_MINIMAP_WIDTH = 112.0F;
constexpr float FIXED_SCROLLBAR_WIDTH = 14.0F;
constexpr float MIN_PANE_WIDTH_FOR_MINIMAP = 120.0F;

COLORREF to_color_ref(const UI::Theme::Color& color)
{
    return RGB(color.red, color.green, color.blue);
}

std::string make_lsp_uri(std::string_view filename)
{
    if (filename.empty() || filename.starts_with("Untitled") || filename.starts_with("untitled"))
    {
        return "file:///untitled.cpp";
    }
    std::error_code ec;
    std::filesystem::path p(filename);
    if (!p.is_absolute())
    {
        p = std::filesystem::current_path() / p;
    }
    p = std::filesystem::weakly_canonical(p, ec);
    std::string generic = p.generic_string();
    if (!generic.starts_with("/"))
    {
        generic = "/" + generic;
    }
    return "file://" + generic;
}

bool is_utf8_continuation(char character)
{
    return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
}

std::size_t next_character_column(std::string_view line, std::size_t column)
{
    column = std::min(column + 1, line.size());
    while (column < line.size() && is_utf8_continuation(line[column]))
    {
        ++column;
    }
    return column;
}

bool has_gutter_marker(std::string_view line)
{
    return line.find("namespace ") != std::string_view::npos ||
        (line.find("::") != std::string_view::npos && line.find('(') != std::string_view::npos);
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

std::pair<std::optional<UI::Editor::TextPosition>, std::optional<UI::Editor::TextPosition>>
find_enclosing_braces(const UI::Editor::TextDocumentModel& document)
{
    const std::size_t start_line = document.get_caret_line();
    const std::size_t start_col = document.get_caret_column();
    
    std::optional<UI::Editor::TextPosition> open_brace;
    std::optional<UI::Editor::TextPosition> close_brace;
    
    // Simplistic backward search for unmatched '{'
    int brace_depth = 0;
    bool found_open = false;
    
    // Limit backward search to ~500 lines to avoid freezing on massive files without braces
    const int search_limit = std::max(0, static_cast<int>(start_line) - 500);
    
    for (int line_idx = static_cast<int>(start_line); line_idx >= search_limit; --line_idx)
    {
        std::string_view line = document.get_line(static_cast<std::size_t>(line_idx));
        
        // Start searching from the character AT the cursor (start_col) or the end of the line
        int search_end = static_cast<int>(line.size());
        if (line_idx == static_cast<int>(start_line))
        {
            search_end = std::min(static_cast<int>(start_col) + 1, search_end);
        }
        
        for (int i = search_end - 1; i >= 0; --i)
        {
            if (line[i] == '}') {
                ++brace_depth;
            }
            else if (line[i] == '{') {
                if (brace_depth > 0) {
                    --brace_depth;
                } else {
                    open_brace = UI::Editor::TextPosition{static_cast<std::size_t>(line_idx), static_cast<std::size_t>(i)};
                    found_open = true;
                    break;
                }
            }
        }
        if (found_open) break;
    }
    
    // Simplistic forward search for matching '}' starting from the open brace
    if (found_open)
    {
        brace_depth = 0;
        bool found_close = false;
        const std::size_t doc_lines = document.get_line_count();
        const std::size_t forward_limit = std::min(doc_lines, open_brace->line + 1500);
        
        for (std::size_t line_idx = open_brace->line; line_idx < forward_limit; ++line_idx)
        {
            std::string_view line = document.get_line(line_idx);
            std::size_t search_start = (line_idx == open_brace->line) ? open_brace->column : 0;
            
            for (std::size_t i = search_start; i < line.size(); ++i)
            {
                if (line[i] == '{') {
                    ++brace_depth;
                }
                else if (line[i] == '}') {
                    --brace_depth;
                    if (brace_depth == 0) {
                        close_brace = UI::Editor::TextPosition{line_idx, i};
                        found_close = true;
                        break;
                    }
                }
            }
            if (found_close) break;
        }
    }
    
    return {open_brace, close_brace};
}

} // namespace

std::string TextEditor::get_active_document_uri() const
{
    if (const auto* path_ptr = m_controller.get_active_path())
    {
        return make_lsp_uri(path_ptr->string());
    }
    if (const auto* doc = m_controller.get_active_document())
    {
        return make_lsp_uri(doc->get_file_name());
    }
    return "file:///untitled.cpp";
}

std::string TextEditor::get_active_document_filename() const
{
    if (const auto* path_ptr = m_controller.get_active_path())
    {
        return path_ptr->string();
    }
    if (const auto* doc = m_controller.get_active_document())
    {
        return std::string(doc->get_file_name());
    }
    return "untitled.cpp";
}

void TextEditor::on_diagnostics_updated(const std::string& uri, std::vector<Language::Protocol::Diagnostic> diags)
{
    std::lock_guard<std::mutex> lock(m_lsp_mutex);
    const std::string active_uri = get_active_document_uri();
    if (uri == active_uri || uri.ends_with(get_active_document_filename()))
    {
        if (auto* doc = m_controller.get_active_document())
        {
            doc->set_diagnostics(std::move(diags));
        }
        if (m_window_handle != nullptr)
        {
            InvalidateRect(m_window_handle, nullptr, FALSE);
        }
    }
}

bool TextEditor::open_file(const std::filesystem::path& path)
{
    const bool opened = m_controller.open_file(path);
    if (opened)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
        m_hovered_fold_line.reset();

        if (const auto* doc = m_controller.get_active_document(); doc != nullptr)
        {
            const std::string uri = get_active_document_uri();
            const std::string fname = get_active_document_filename();
            std::string content;
            std::size_t approx_size = 0;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                approx_size += doc->get_line(i).size() + 1;
            }
            content.reserve(approx_size);
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                content += doc->get_line(i);
                content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_opened(
                uri, fname, 1, content);

            auto diags = Language::LanguageServerManager::instance().get_diagnostics_for_document(uri);
            if (!diags.empty())
            {
                const_cast<UI::Editor::TextDocumentModel*>(doc)->set_diagnostics(std::move(diags));
            }
        }
    }
    return opened;
}

bool TextEditor::open_file_at_location(const std::filesystem::path& path, std::size_t line, std::size_t column)
{
    const bool opened = open_file(path);
    if (!opened)
    {
        return false;
    }
    if (auto* doc = const_cast<UI::Editor::TextDocumentModel*>(m_controller.get_active_document()))
    {
        const std::size_t line_idx = (line > 0) ? (line - 1) : 0;
        doc->set_caret(line_idx, column, false);
        m_reveal_caret_pending = true;
        if (line_idx > 5)
        {
            static_cast<void>(m_scrollbar.scroll_to(line_idx - 5));
        }
        else
        {
            static_cast<void>(m_scrollbar.scroll_to(0));
        }
    }
    return true;
}

bool TextEditor::close_file(const std::filesystem::path& path)
{
    const auto docs = m_controller.get_documents();
    for (std::size_t i = 0; i < docs.size(); ++i)
    {
        if (docs[i].path == path)
        {
            return m_controller.close_file(i);
        }
    }
    return false;
}

bool TextEditor::close_all_files()
{
    m_split_document_index.reset();
    m_is_split = false;
    m_is_resizing_split = false;
    m_completion_popup.hide();
    m_hover_tooltip.hide();
    m_signature_help.hide();
    m_tab_scroll_offset = 0.0f;
    m_text_scroll_offset = 0.0f;
    m_scrollbar.reset();
    m_split_scrollbar.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
    m_hovered_fold_line.reset();
    return m_controller.close_all_files();
}

std::size_t TextEditor::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths)
{
    const std::size_t opened_count = m_controller.open_dropped_paths(dropped_paths);
    if (opened_count > 0)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_focused = true;
        m_caret_blink.reset();
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
        m_hovered_fold_line.reset();

        if (const auto* doc = m_controller.get_active_document(); doc != nullptr)
        {
            const std::string uri = get_active_document_uri();
            const std::string fname = get_active_document_filename();
            std::string content;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                content += doc->get_line(i);
                content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_opened(
                uri, fname, 1, content);

            auto diags = Language::LanguageServerManager::instance().get_diagnostics_for_document(uri);
            if (!diags.empty())
            {
                const_cast<UI::Editor::TextDocumentModel*>(doc)->set_diagnostics(std::move(diags));
            }
        }
    }
    return opened_count;
}

bool TextEditor::create_buffer()
{
    const bool created = m_controller.create_buffer();
    if (created)
    {
        m_scrollbar.reset();
        m_reveal_caret_pending = true;
        m_focused = true;
        m_caret_blink.reset();
        m_hovered_tab_index.reset();
        m_hovered_tab_close_index.reset();
        m_hovered_fold_line.reset();

        if (const auto* doc = m_controller.get_active_document(); doc != nullptr)
        {
            const std::string uri = get_active_document_uri();
            const std::string fname = get_active_document_filename();
            std::string content;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                content += doc->get_line(i);
                content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_opened(
                uri, fname, 1, content);

            auto diags = Language::LanguageServerManager::instance().get_diagnostics_for_document(uri);
            if (!diags.empty())
            {
                const_cast<UI::Editor::TextDocumentModel*>(doc)->set_diagnostics(std::move(diags));
            }
        }
    }
    return created;
}

void TextEditor::close_all_documents()
{
    while (!m_controller.get_documents().empty())
    {
        static_cast<void>(m_controller.close_file(0));
    }
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
    if (m_window_handle)
    {
        InvalidateRect(m_window_handle, nullptr, FALSE);
    }
}

void TextEditor::close_saved_documents()
{
    const auto docs = m_controller.get_documents();
    for (std::size_t i = docs.size(); i > 0; --i)
    {
        if (!docs[i - 1].text.is_dirty())
        {
            static_cast<void>(m_controller.close_file(i - 1));
        }
    }
    m_scrollbar.reset();
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    m_hovered_tab_index.reset();
    m_hovered_tab_close_index.reset();
    if (m_window_handle)
    {
        InvalidateRect(m_window_handle, nullptr, FALSE);
    }
}

void TextEditor::show_tab_action_menu(const UI::Editor::StudioEditorLayoutResult& layout)
{
    m_tab_action_menu.items = {
        {"Stage Changes", "", false, false, false},
        {"Show Opened Editors", "", false, false, false},
        {"", "", true, false, false},
        {"Close All", "Ctrl+K W", false, false, false},
        {"Close Saved", "Ctrl+K U", false, false, false},
        {"", "", true, false, false},
        {"Enable Preview Editors", "", false, m_preview_editors_enabled, true},
        {"", "", true, false, false},
        {"Lock Group", "", false, false, false},
        {"Configure Editors", "", false, false, false}
    };

    const float scale = layout.dpi_scale;
    const float row_height = 24.0F * scale;
    const float sep_height = 7.0F * scale;
    const float vertical_padding = 4.0F * scale;
    float total_h = vertical_padding * 2.0F;
    float popup_width = 190.0F * scale;

    for (const auto& item : m_tab_action_menu.items)
    {
        if (item.is_separator)
        {
            total_h += sep_height;
            continue;
        }
        total_h += row_height;
        float item_width = static_cast<float>(item.label.size()) * 7.0F * scale + 44.0F * scale;
        if (!item.shortcut.empty())
        {
            item_width += static_cast<float>(item.shortcut.size()) * 7.0F * scale + 30.0F * scale;
        }
        popup_width = std::max(popup_width, item_width);
    }
    popup_width = std::min(popup_width, 360.0F * scale);

    float popup_x = m_tab_action_bounds[3].right() - popup_width;
    if (popup_x < layout.workspace_bounds.x + 8.0F * scale)
    {
        popup_x = layout.workspace_bounds.x + 8.0F * scale;
    }
    if (popup_x + popup_width > layout.workspace_bounds.right() - 8.0F * scale)
    {
        popup_x = layout.workspace_bounds.right() - popup_width - 8.0F * scale;
    }
    const float popup_y = layout.editor_header_bounds.bottom() + 6.0F * scale;

    m_tab_action_menu.bounds = UI::Rect{popup_x, popup_y, popup_width, total_h};
    m_tab_action_menu.item_bounds.clear();
    m_tab_action_menu.hovered_index.reset();

    float current_y = popup_y + vertical_padding;
    for (const auto& item : m_tab_action_menu.items)
    {
        if (item.is_separator)
        {
            m_tab_action_menu.item_bounds.push_back(
                UI::Rect{popup_x, current_y, popup_width, sep_height});
            current_y += sep_height;
        }
        else
        {
            m_tab_action_menu.item_bounds.push_back(
                UI::Rect{popup_x, current_y, popup_width, row_height});
            current_y += row_height;
        }
    }

    m_tab_action_menu.visible = true;
    if (m_window_handle)
    {
        InvalidateRect(m_window_handle, nullptr, FALSE);
    }
}

void TextEditor::draw_editor_header(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        return;
    }

    const float scale = surface.m_dpi_scale;
    const auto& header_bounds = layout.editor_header_bounds;
    if (header_bounds.is_empty() || header_bounds.height <= 2.0F)
    {
        return;
    }

    // 1. Container background (sleek header bar directly above gutter and code)
    surface.fill_rectangle(device_context, header_bounds, surface.m_palette.editor_background);

    const float center_y = header_bounds.y + header_bounds.height * 0.5F;
    const float button_w = 26.0F * scale;
    const float button_h = 22.0F * scale;
    const float button_y = center_y - button_h * 0.5F;

    if (m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
        const UI::Rect left_header{header_bounds.x, header_bounds.y, splitter_x - header_bounds.x, header_bounds.height};
        const UI::Rect right_header{splitter_x + 2.0F * scale, header_bounds.y, header_bounds.right() - (splitter_x + 2.0F * scale), header_bounds.height};

        // Solid Background Fills for both headers
        surface.fill_rectangle(device_context, left_header, surface.m_palette.editor_background);
        surface.fill_rectangle(device_context, right_header, surface.m_palette.editor_background);

        // Left Header File Title with Icon
        SaveDC(device_context);
        IntersectClipRect(device_context,
            round_to_int(left_header.x), round_to_int(left_header.y),
            round_to_int(left_header.right() - 4.0F * scale), round_to_int(left_header.bottom()));

        const std::string left_filename{document->get_file_name()};
        const std::string left_icon = UI::Editor::file_icon_asset_for_path(std::filesystem::path{left_filename});
        const int left_icon_sz = std::max(round_to_int(13.0F * scale), 11);
        const float left_icon_cx = left_header.x + 12.0F * scale + left_icon_sz * 0.5F;
        surface.draw_svg_icon(
            device_context, "Assets/icons/" + left_icon,
            round_to_int(left_icon_cx), round_to_int(center_y), left_icon_sz,
            surface.m_palette.text_muted, surface.m_palette.editor_background, true);

        surface.draw_text(
            device_context, *surface.m_small_font, left_filename,
            left_header.x + 12.0F * scale + left_icon_sz + 6.0F * scale, center_y,
            surface.m_palette.text_primary);
        RestoreDC(device_context, -1);

        // Vertical Splitter Line in Header (matches editor splitter exactly)
        const UI::Theme::Color splitter_col = (m_is_resizing_split || m_hovered_split_resize)
            ? UI::Theme::Color{53, 132, 228, 255}
            : surface.m_palette.border;
        surface.fill_rectangle(
            device_context,
            UI::Rect{splitter_x, header_bounds.y, 2.0F * scale, header_bounds.height},
            splitter_col);

        // Close Split Button (Using diagnostic-error.svg icon)
        m_split_close_btn_bounds = UI::Rect{right_header.right() - button_w - 4.0F * scale, button_y, button_w, button_h};
        if (m_hovered_split_close)
        {
            surface.fill_rounded_rectangle(device_context, m_split_close_btn_bounds, UI::Theme::Color{255, 255, 255, 25}, 4.0F * scale);
        }
        const UI::Theme::Color cross_col = m_hovered_split_close ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted;
        const int cross_cx = round_to_int(m_split_close_btn_bounds.x + m_split_close_btn_bounds.width * 0.5F);
        const int cross_cy = round_to_int(m_split_close_btn_bounds.y + m_split_close_btn_bounds.height * 0.5F);
        const int cross_icon_sz = std::max(round_to_int(12.0F * scale), 10);
        surface.draw_svg_icon(
            device_context, "Assets/icons/diagnostic-error.svg", cross_cx, cross_cy, cross_icon_sz,
            cross_col, surface.m_palette.editor_background);

        // 4 Action Buttons placed before close split button
        const float actions_right = m_split_close_btn_bounds.x - 2.0F * scale;
        m_tab_action_bounds[3] = UI::Rect{actions_right - 1.0F * button_w, button_y, button_w, button_h};
        m_tab_action_bounds[2] = UI::Rect{actions_right - 2.0F * button_w - 2.0F * scale, button_y, button_w, button_h};
        m_tab_action_bounds[1] = UI::Rect{actions_right - 3.0F * button_w - 4.0F * scale, button_y, button_w, button_h};
        m_tab_action_bounds[0] = UI::Rect{actions_right - 4.0F * button_w - 6.0F * scale, button_y, button_w, button_h};

        // Right Header File Title with Icon (Clipped before buttons)
        SaveDC(device_context);
        IntersectClipRect(device_context,
            round_to_int(right_header.x), round_to_int(right_header.y),
            round_to_int(m_tab_action_bounds[0].x - 4.0F * scale), round_to_int(right_header.bottom()));

        const auto* split_doc = m_controller.get_document(*m_split_document_index);
        if (split_doc != nullptr)
        {
            const std::string right_filename{split_doc->get_file_name()};
            const std::string right_icon = UI::Editor::file_icon_asset_for_path(std::filesystem::path{right_filename});
            const int right_icon_sz = std::max(round_to_int(13.0F * scale), 11);
            const float right_icon_cx = right_header.x + 12.0F * scale + right_icon_sz * 0.5F;
            surface.draw_svg_icon(
                device_context, "Assets/icons/" + right_icon,
                round_to_int(right_icon_cx), round_to_int(center_y), right_icon_sz,
                surface.m_palette.text_muted, surface.m_palette.editor_background, true);

            surface.draw_text(
                device_context, *surface.m_small_font, right_filename,
                right_header.x + 12.0F * scale + right_icon_sz + 6.0F * scale, center_y,
                surface.m_palette.text_primary);
        }
        RestoreDC(device_context, -1);
    }
    else
    {
        m_split_close_btn_bounds = UI::Rect{};

        // Right side: The 4 Action Buttons aligned to the right of header_bounds
        const float actions_right = header_bounds.right() - 4.0F * scale;
        m_tab_action_bounds[3] = UI::Rect{actions_right - 1.0F * button_w, button_y, button_w, button_h};
        m_tab_action_bounds[2] = UI::Rect{actions_right - 2.0F * button_w - 2.0F * scale, button_y, button_w, button_h};
        m_tab_action_bounds[1] = UI::Rect{actions_right - 3.0F * button_w - 4.0F * scale, button_y, button_w, button_h};
        m_tab_action_bounds[0] = UI::Rect{actions_right - 4.0F * button_w - 6.0F * scale, button_y, button_w, button_h};

        // Left side: File title with Icon (Clipped before buttons)
        SaveDC(device_context);
        IntersectClipRect(device_context,
            round_to_int(header_bounds.x), round_to_int(header_bounds.y),
            round_to_int(m_tab_action_bounds[0].x - 4.0F * scale), round_to_int(header_bounds.bottom()));

        const std::string filename{document->get_file_name()};
        const std::string icon_asset = UI::Editor::file_icon_asset_for_path(std::filesystem::path{filename});
        const int icon_sz = std::max(round_to_int(13.0F * scale), 11);
        const float icon_cx = header_bounds.x + 12.0F * scale + icon_sz * 0.5F;
        surface.draw_svg_icon(
            device_context, "Assets/icons/" + icon_asset,
            round_to_int(icon_cx), round_to_int(center_y), icon_sz,
            surface.m_palette.text_muted, surface.m_palette.editor_background, true);

        surface.draw_text(
            device_context, *surface.m_small_font, filename,
            header_bounds.x + 12.0F * scale + icon_sz + 6.0F * scale, center_y,
            surface.m_palette.text_primary);
        RestoreDC(device_context, -1);
    }

    const char* icons[] = {
        "split-right.svg",
        "arrow-left.svg",
        "arrow-right.svg",
        "ellipsis.svg"
    };
    const int icon_sizes[] = {
        std::max(round_to_int(13.0F * scale), 11),
        std::max(round_to_int(12.0F * scale), 10),
        std::max(round_to_int(12.0F * scale), 10),
        std::max(round_to_int(13.0F * scale), 11)
    };

    for (std::size_t i = 0; i < 4; ++i)
    {
        const UI::Rect& btn = m_tab_action_bounds[i];
        const bool is_hovered = (m_hovered_tab_action && *m_hovered_tab_action == i);
        const bool is_active_menu = (i == 3 && m_tab_action_menu.visible) || (i == 0 && m_is_split);

        if (is_active_menu || is_hovered)
        {
            surface.fill_rounded_rectangle(
                device_context, btn,
                is_active_menu ? UI::Theme::Color{255, 255, 255, 36} : UI::Theme::Color{255, 255, 255, 20},
                4.0F * scale);
        }

        const int cx = round_to_int(btn.x + btn.width * 0.5F);
        const int cy = round_to_int(btn.y + btn.height * 0.5F);
        const UI::Theme::Color icon_col = (is_hovered || is_active_menu)
            ? UI::Theme::Color{255, 255, 255, 255}
            : surface.m_palette.text_muted;

        surface.draw_svg_icon(
            device_context, icons[i], cx, cy, icon_sizes[i],
            icon_col, surface.m_palette.editor_background);
    }

    // 2. Crisp Hairline bottom separator border
    const int y_bot = round_to_int(header_bounds.bottom()) - 1;
    surface.draw_line(
        device_context,
        round_to_int(header_bounds.x), y_bot,
        round_to_int(header_bounds.right()), y_bot,
        surface.m_palette.border);
}

void TextEditor::draw_tab_action_menu(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    (void)layout;
    if (!m_tab_action_menu.visible) return;

    const float scale = surface.m_dpi_scale;
    const auto& bounds = m_tab_action_menu.bounds;
    const int radius = std::max(round_to_int(6.0F * scale), 5);

    // 1. Soft, realistic multi-layer elevation drop shadow
    struct ShadowLayer {
        float dx;
        float dy;
        float spread;
        uint8_t alpha;
    };
    const ShadowLayer shadow_layers[] = {
        {0.0F, 5.0F, 12.0F, 16},
        {0.0F, 2.5F,  6.0F, 24},
        {0.0F, 1.0F,  2.0F, 36},
    };
    for (const auto &layer : shadow_layers)
    {
        const float spread = layer.spread * scale;
        const UI::Rect layer_rect{
            bounds.x - spread + layer.dx * scale,
            bounds.y - spread + layer.dy * scale,
            bounds.width + spread * 2.0F,
            bounds.height + spread * 2.0F,
        };
        surface.fill_rounded_rectangle(device_context, layer_rect,
                                       UI::Theme::Color{0, 0, 0, layer.alpha},
                                       static_cast<float>(radius) + spread);
    }

    // 2. Elevated Floating Card background (rich dark slate)
    surface.fill_rounded_rectangle(device_context, bounds,
                                   UI::Theme::Color{26, 28, 34, 255},
                                   static_cast<float>(radius));

    // 3. Crisp modern floating hairline border
    const RECT native_bounds{
        round_to_int(bounds.x),
        round_to_int(bounds.y),
        round_to_int(bounds.right()),
        round_to_int(bounds.bottom())
    };
    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(65, 70, 85));
    HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
    HGDIOBJ previous_pen = SelectObject(device_context, border_pen);
    RoundRect(device_context, native_bounds.left, native_bounds.top,
              native_bounds.right, native_bounds.bottom, radius * 2, radius * 2);
    SelectObject(device_context, previous_pen);
    SelectObject(device_context, previous_brush);
    DeleteObject(border_pen);

    // 4. Draw items
    for (std::size_t i = 0; i < m_tab_action_menu.items.size() && i < m_tab_action_menu.item_bounds.size(); ++i)
    {
        const auto& item = m_tab_action_menu.items[i];
        const auto& item_bounds = m_tab_action_menu.item_bounds[i];

        if (item.is_separator)
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{
                    item_bounds.x + 10.0F * scale,
                    item_bounds.y + item_bounds.height * 0.5F,
                    item_bounds.width - 20.0F * scale,
                    1.0F
                },
                surface.m_palette.border);
            continue;
        }

        const bool hovered = (m_tab_action_menu.hovered_index && *m_tab_action_menu.hovered_index == i);
        if (hovered)
        {
            UI::Rect hover_bounds = item_bounds;
            hover_bounds.x += 5.0F * scale;
            hover_bounds.width -= 10.0F * scale;
            hover_bounds.y += 1.0F * scale;
            hover_bounds.height -= 2.0F * scale;
            surface.fill_rounded_rectangle(
                device_context, hover_bounds,
                UI::Theme::Color{53, 132, 228, 240},
                std::max(4.0F * scale, 3.0F));
        }

        // If item has checkbox (e.g. Enable Preview Editors)
        if (item.has_checkbox && item.is_checked)
        {
            const int check_x = round_to_int(item_bounds.x + 12.0F * scale);
            const int check_y = round_to_int(item_bounds.y + item_bounds.height * 0.5F);
            surface.draw_svg_icon(
                device_context, "check.svg", check_x, check_y,
                std::max(round_to_int(11.0F * scale), 9),
                hovered ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_primary,
                surface.m_palette.tab_active_background);
        }

        const float text_x = item_bounds.x + 24.0F * scale;
        const float text_y = item_bounds.y + item_bounds.height * 0.5F;
        surface.draw_text(
            device_context, *surface.m_small_font, item.label, text_x, text_y,
            hovered ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_primary);

        if (!item.shortcut.empty())
        {
            const int sc_w = surface.get_text_width(device_context, *surface.m_small_font, item.shortcut);
            const float sc_x = item_bounds.right() - static_cast<float>(sc_w) - 12.0F * scale;
            surface.draw_text(
                device_context, *surface.m_small_font, item.shortcut, sc_x, text_y,
                hovered ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted);
        }
    }
}

void TextEditor::draw_split_drop_overlay(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!m_tab_drag_drop.is_dragging()) return;

    const float scale = surface.m_dpi_scale;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    const std::size_t dragged_idx = m_tab_drag_drop.get_dragged_index();
    if (dragged_idx >= documents.size()) return;

    // 1. Draw VS Code Glowing Drop Zone Target Overlay
    if (m_active_drop_zone != SplitDropZone::None)
    {
        const float full_x = layout.editor_bounds.x;
        const float full_y = layout.editor_header_bounds.y;
        const float full_w = layout.editor_bounds.width;
        const float full_h = layout.editor_bounds.height + layout.editor_header_bounds.height;

        UI::Rect zone_rect = layout.editor_bounds;
        std::string zone_label = "Split Right";

        if (m_active_drop_zone == SplitDropZone::Right)
        {
            zone_rect = UI::Rect{full_x + full_w * 0.5F, full_y, full_w * 0.5F, full_h};
            zone_label = "Split Right";
        }
        else if (m_active_drop_zone == SplitDropZone::Left)
        {
            zone_rect = UI::Rect{full_x, full_y, full_w * 0.5F, full_h};
            zone_label = "Split Left";
        }
        else if (m_active_drop_zone == SplitDropZone::Bottom)
        {
            zone_rect = UI::Rect{full_x, full_y + full_h * 0.5F, full_w, full_h * 0.5F};
            zone_label = "Split Down";
        }
        else if (m_active_drop_zone == SplitDropZone::Top)
        {
            zone_rect = UI::Rect{full_x, full_y, full_w, full_h * 0.5F};
            zone_label = "Split Up";
        }
        else if (m_active_drop_zone == SplitDropZone::Center)
        {
            zone_rect = UI::Rect{full_x, full_y, full_w, full_h};
            zone_label = "Open Here";
        }

        // Translucent blue accent fill
        surface.fill_rounded_rectangle(
            device_context, zone_rect,
            UI::Theme::Color{53, 132, 228, 55},
            6.0F * scale);

        // Glowing border
        const RECT native_zone{
            round_to_int(zone_rect.x),
            round_to_int(zone_rect.y),
            round_to_int(zone_rect.right()),
            round_to_int(zone_rect.bottom())
        };
        HPEN zone_pen = CreatePen(PS_SOLID, 2, RGB(53, 132, 228));
        HGDIOBJ prev_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
        HGDIOBJ prev_pen = SelectObject(device_context, zone_pen);
        RoundRect(device_context, native_zone.left, native_zone.top,
                  native_zone.right, native_zone.bottom, 12, 12);
        SelectObject(device_context, prev_pen);
        SelectObject(device_context, prev_brush);
        DeleteObject(zone_pen);

        // Center split badge
        const float badge_w = 140.0F * scale;
        const float badge_h = 32.0F * scale;
        const UI::Rect badge_rect{
            zone_rect.x + (zone_rect.width - badge_w) * 0.5F,
            zone_rect.y + (zone_rect.height - badge_h) * 0.5F,
            badge_w,
            badge_h
        };
        surface.fill_rounded_rectangle(
            device_context, badge_rect,
            UI::Theme::Color{20, 24, 34, 235},
            6.0F * scale);

        const int cx = round_to_int(badge_rect.x + 20.0F * scale);
        const int cy = round_to_int(badge_rect.y + badge_h * 0.5F);
        surface.draw_svg_icon(
            device_context, "split-right.svg", cx, cy,
            std::max(round_to_int(14.0F * scale), 12),
            UI::Theme::Color{53, 132, 228, 255},
            UI::Theme::Color{20, 24, 34, 255});

        surface.draw_text(
            device_context, *surface.m_small_font, zone_label,
            badge_rect.x + 36.0F * scale,
            badge_rect.y + badge_h * 0.5F,
            UI::Theme::Color{255, 255, 255, 255});
    }

    // 2. Floating Dragged Tab Pill attached to cursor
    if (m_drag_cursor_y > layout.tab_bar_bounds.bottom())
    {
        const std::string_view fname_view = documents[dragged_idx].text.get_file_name();
        const std::string fname{fname_view};
        const int text_w = surface.get_text_width(device_context, *surface.m_small_font, fname);
        const float pill_w = static_cast<float>(text_w) + 40.0F * scale;
        const float pill_h = 24.0F * scale;
        const UI::Rect pill_rect{
            m_drag_cursor_x + 12.0F * scale,
            m_drag_cursor_y + 12.0F * scale,
            pill_w,
            pill_h
        };

        // Shadow
        surface.fill_rounded_rectangle(
            device_context,
            UI::Rect{pill_rect.x + 2.0F, pill_rect.y + 3.0F, pill_rect.width, pill_rect.height},
            UI::Theme::Color{0, 0, 0, 100},
            4.0F * scale);

        // Background
        surface.fill_rounded_rectangle(
            device_context, pill_rect,
            UI::Theme::Color{30, 32, 42, 240},
            4.0F * scale);

        // Border
        const RECT native_pill{
            round_to_int(pill_rect.x),
            round_to_int(pill_rect.y),
            round_to_int(pill_rect.right()),
            round_to_int(pill_rect.bottom())
        };
        HPEN pill_pen = CreatePen(PS_SOLID, 1, RGB(80, 140, 220));
        HGDIOBJ prev_brush = SelectObject(device_context, GetStockObject(NULL_BRUSH));
        HGDIOBJ prev_pen = SelectObject(device_context, pill_pen);
        RoundRect(device_context, native_pill.left, native_pill.top,
                  native_pill.right, native_pill.bottom, 8, 8);
        SelectObject(device_context, prev_pen);
        SelectObject(device_context, prev_brush);
        DeleteObject(pill_pen);

        // Icon + Text
        const int px = round_to_int(pill_rect.x + 12.0F * scale);
        const int py = round_to_int(pill_rect.y + pill_h * 0.5F);
        surface.draw_svg_icon(
            device_context, "file.svg", px, py,
            std::max(round_to_int(12.0F * scale), 10),
            UI::Theme::Color{53, 132, 228, 255},
            UI::Theme::Color{30, 32, 42, 255});

        surface.draw_text(
            device_context, *surface.m_small_font, fname,
            pill_rect.x + 24.0F * scale,
            pill_rect.y + pill_h * 0.5F,
            UI::Theme::Color{255, 255, 255, 255});
    }
}

bool TextEditor::is_split_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!m_is_split || !m_split_document_index.has_value() || *m_split_document_index >= m_controller.get_documents().size())
    {
        return false;
    }
    const float scale = layout.dpi_scale;
    const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    // Check if mouse is on left scrollbar or minimap - if so, it's NOT a splitter resize point!
    const float scroll_top_y = layout.editor_bounds.y;
    const float scroll_total_h = layout.editor_bounds.height;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
    const float left_minimap_w = (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
    const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y, left_minimap_w, scroll_total_h};

    const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
    const float right_minimap_w = (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
    const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
    const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w, scroll_top_y, right_minimap_w, scroll_total_h};

    if (left_scrollbar.contains(point_x, point_y) || left_minimap.contains(point_x, point_y) ||
        right_scrollbar.contains(point_x, point_y) || right_minimap.contains(point_x, point_y))
    {
        return false;
    }

    const float grab_margin = 3.0F * scale;
    const bool x_in_range = (point_x >= splitter_x - grab_margin) && (point_x <= splitter_x + 2.0F * scale + grab_margin);
    const bool y_in_range = (point_y >= layout.editor_header_bounds.y) && (point_y <= layout.editor_bounds.bottom());
    return x_in_range && y_in_range;
}

bool TextEditor::is_fold_margin_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    const float scale = layout.dpi_scale;
    const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
    const float left_fold_margin_left = layout.gutter_bounds.right() - fold_margin;
    if (layout.gutter_bounds.contains(point_x, point_y) && point_x >= left_fold_margin_left)
    {
        return true;
    }

    if (m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
        const float right_gutter_w = layout.gutter_bounds.width;
        const float right_gutter_x = splitter_x + 2.0F * scale;
        const float right_fold_margin_left = right_gutter_x + right_gutter_w - fold_margin;
        const UI::Rect right_gutter{right_gutter_x, layout.editor_bounds.y, right_gutter_w, layout.editor_bounds.height};
        if (right_gutter.contains(point_x, point_y) && point_x >= right_fold_margin_left)
        {
            return true;
        }
    }

    return false;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (m_tab_action_menu.visible && m_tab_action_menu.bounds.contains(point_x, point_y))
    {
        return true;
    }
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (m_tab_action_bounds[i].contains(point_x, point_y))
        {
            return true;
        }
    }

    if (layout.editor_header_bounds.contains(point_x, point_y))
    {
        return true;
    }

    if (!layout.tab_bar_bounds.contains(point_x, point_y))
    {
        return false;
    }

    const float right_limit = layout.tab_bar_bounds.right();
    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    for (const UI::Editor::EditorSessionDocument& document : documents)
    {
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.get_text_width(
                device_context,
                *surface.m_ui_font,
                document.text.get_file_name())),
            surface.m_dpi_scale);
        if (tab_x > right_limit)
        {
            break;
        }
        const UI::Rect bounds{
            tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        if (bounds.contains(point_x, point_y))
        {
            return true;
        }
        tab_x += width +
            UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }
    return false;
}

bool TextEditor::is_empty_state_interactive_point(
    float point_x,
    float point_y) const noexcept
{
    if (m_controller.get_active_document() != nullptr)
        return false;
    return m_empty_state_open_btn.get_bounds().contains(point_x, point_y) ||
           m_empty_state_clone_btn.get_bounds().contains(point_x, point_y);
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    bool extend_selection,
    std::string& command_out)
{
    if (m_tab_action_menu.visible)
    {
        if (m_tab_action_menu.bounds.contains(point_x, point_y))
        {
            for (std::size_t i = 0; i < m_tab_action_menu.item_bounds.size(); ++i)
            {
                if (!m_tab_action_menu.items[i].is_separator &&
                    m_tab_action_menu.item_bounds[i].contains(point_x, point_y))
                {
                    if (i == 0) {
                        std::clog << "[ZDE] Stage Changes requested\n";
                    } else if (i == 1) {
                        std::clog << "[ZDE] Show Opened Editors requested\n";
                    } else if (i == 3) {
                        const_cast<TextEditor*>(this)->close_all_documents();
                    } else if (i == 4) {
                        const_cast<TextEditor*>(this)->close_saved_documents();
                    } else if (i == 6) {
                        const_cast<TextEditor*>(this)->m_preview_editors_enabled = !m_preview_editors_enabled;
                        const_cast<TextEditor*>(this)->m_tab_action_menu.items[6].is_checked = m_preview_editors_enabled;
                    } else if (i == 8) {
                        std::clog << "[ZDE] Lock Group requested\n";
                    } else if (i == 9) {
                        std::clog << "[ZDE] Configure Editors requested\n";
                    }
                    break;
                }
            }
            const_cast<TextEditor*>(this)->m_tab_action_menu.visible = false;
            if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
            return true;
        }
        else
        {
            const_cast<TextEditor*>(this)->m_tab_action_menu.visible = false;
            if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        }
    }

    if (m_is_split && m_split_close_btn_bounds.contains(point_x, point_y))
    {
        m_is_split = false;
        m_split_document_index.reset();
        if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        return true;
    }



    for (std::size_t i = 0; i < 4; ++i)
    {
        if (m_tab_action_bounds[i].contains(point_x, point_y))
        {
            if (i == 0)
            {
                m_is_split = !m_is_split;
                if (m_is_split)
                {
                    const auto doc_count = m_controller.get_documents().size();
                    if (doc_count > 1)
                    {
                        const std::size_t active = m_controller.get_active_index().value_or(0);
                        m_split_document_index = (active + 1) % doc_count;
                    }
                    else
                    {
                        m_split_document_index = m_controller.get_active_index().value_or(0);
                    }
                    std::clog << "[ZDE] Split Editor Right activated\n";
                }
                else
                {
                    m_split_document_index.reset();
                    std::clog << "[ZDE] Split Editor Right closed\n";
                }
            }
            else if (i == 1)
            {
                const auto doc_count = m_controller.get_documents().size();
                if (doc_count > 1)
                {
                    std::size_t active = m_controller.get_active_index().value_or(0);
                    std::size_t prev = (active == 0) ? (doc_count - 1) : (active - 1);
                    static_cast<void>(m_controller.activate_file(prev));
                    m_scrollbar.reset();
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                }
            }
            else if (i == 2)
            {
                const auto doc_count = m_controller.get_documents().size();
                if (doc_count > 1)
                {
                    std::size_t active = m_controller.get_active_index().value_or(0);
                    std::size_t next = (active + 1) % doc_count;
                    static_cast<void>(m_controller.activate_file(next));
                    m_scrollbar.reset();
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                }
            }
            else if (i == 3)
            {
                const_cast<TextEditor*>(this)->show_tab_action_menu(layout);
            }
            if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
            return true;
        }
    }

    if (layout.editor_header_bounds.contains(point_x, point_y))
    {
        return true;
    }

    if (layout.tab_bar_bounds.contains(point_x, point_y))
    {
        if (m_max_tab_scroll > 0.0f)
        {
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
            if (hit_bounds.contains(point_x, point_y))
            {
                m_dragging_tab_scrollbar = true;
                m_tab_scroll_drag_start_x = point_x;
                m_tab_scroll_drag_initial_offset = m_tab_scroll_offset;
                return true;
            }
        }

        float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
        const float right_limit = layout.tab_bar_bounds.right();
        const std::span<const UI::Editor::EditorSessionDocument> documents =
            m_controller.get_documents();
        for (std::size_t index = 0; index < documents.size(); ++index)
        {
            const float width = UI::Editor::calculate_editor_tab_width(
                static_cast<float>(surface.get_text_width(
                    device_context,
                    *surface.m_ui_font,
                    documents[index].text.get_file_name())),
                surface.m_dpi_scale);
            if (tab_x > right_limit)
            {
                break;
            }
            const UI::Rect bounds{
                tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
            if (bounds.contains(point_x, point_y))
            {
                const UI::Rect close_bounds{
                    bounds.right() -
                        UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                            surface.m_dpi_scale,
                    bounds.y,
                    UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                        surface.m_dpi_scale,
                    bounds.height};
                m_focused = true;
                if (close_bounds.contains(point_x, point_y))
                {
                    const bool closed = m_controller.close_file(index);
                    if (closed)
                    {
                        m_scrollbar.reset();
                        m_reveal_caret_pending = true;
                        m_caret_blink.reset();
                        m_hovered_tab_index.reset();
                        m_hovered_tab_close_index.reset();
                    }
                    return closed;
                }
                if (m_controller.activate_file(index))
                {
                    m_scrollbar.reset();
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                }
                m_tab_drag_drop.begin_drag(index, point_x);
                m_drag_initial_tab_x = bounds.x;
                return true;
            }
            tab_x += width +
                UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
        }
        // Only consume the titlebar when a real tab/action was hit. Empty
        // space is intentionally left for the native window drag region.
        return false;
    }

    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    const float scale = surface.m_dpi_scale;
    const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
    const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    if (is_split_active)
    {
        const float scroll_top_y = layout.editor_bounds.y;
        const float scroll_total_h = layout.editor_bounds.height;
        const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
        const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
        const float left_minimap_w = (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y, left_minimap_w, scroll_total_h};

        const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
        const float right_minimap_w = (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w, scroll_top_y, right_minimap_w, scroll_total_h};

        auto* left_doc = m_controller.get_active_document();
        auto* right_doc = m_controller.get_document(*m_split_document_index);

        const float line_height = 20.0F * scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));

        // Right Minimap
        if (right_doc != nullptr && right_minimap.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Right;
            UI::Editor::StudioEditorLayoutResult rlay = layout;
            rlay.minimap_bounds = right_minimap;
            rlay.scrollbar_bounds = right_scrollbar;
            m_split_scrollbar.synchronize(right_doc->get_line_count(), visible_count);
            const auto target = m_split_minimap.handle_pointer_press(rlay, point_x, point_y, right_doc->get_line_count(), visible_count, m_split_scrollbar.get_first_visible_line());
            if (target) static_cast<void>(m_split_scrollbar.scroll_to(*target));
            m_focused = true;
            m_pointer_selecting = false;
            m_reveal_caret_pending = false;
            m_caret_blink.reset();
            return true;
        }

        // Right Scrollbar
        if (right_doc != nullptr && right_scrollbar.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Right;
            UI::Editor::StudioEditorLayoutResult rlay = layout;
            rlay.minimap_bounds = right_minimap;
            rlay.scrollbar_bounds = right_scrollbar;
            m_split_scrollbar.synchronize(right_doc->get_line_count(), visible_count);
            m_focused = true;
            m_pointer_selecting = false;
            m_reveal_caret_pending = false;
            m_caret_blink.reset();
            return m_split_scrollbar.handle_pointer_press(rlay, point_x, point_y);
        }

        // Left Minimap
        if (left_doc != nullptr && left_minimap.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Left;
            UI::Editor::StudioEditorLayoutResult llay = layout;
            llay.minimap_bounds = left_minimap;
            llay.scrollbar_bounds = left_scrollbar;
            m_scrollbar.synchronize(left_doc->get_line_count(), visible_count);
            const auto target = m_minimap.handle_pointer_press(llay, point_x, point_y, left_doc->get_line_count(), visible_count, m_scrollbar.get_first_visible_line());
            if (target) static_cast<void>(m_scrollbar.scroll_to(*target));
            m_focused = true;
            m_pointer_selecting = false;
            m_reveal_caret_pending = false;
            m_caret_blink.reset();
            return true;
        }

        // Left Scrollbar
        if (left_doc != nullptr && left_scrollbar.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Left;
            UI::Editor::StudioEditorLayoutResult llay = layout;
            llay.minimap_bounds = left_minimap;
            llay.scrollbar_bounds = left_scrollbar;
            m_scrollbar.synchronize(left_doc->get_line_count(), visible_count);
            m_focused = true;
            m_pointer_selecting = false;
            m_reveal_caret_pending = false;
            m_caret_blink.reset();
            return m_scrollbar.handle_pointer_press(llay, point_x, point_y);
        }

        // Splitter Resize Handle
        if (is_split_resize_handle_point(layout, point_x, point_y))
        {
            m_is_resizing_split = true;
            return true;
        }

        // Right Editor Pane (Code or Gutter)
        if (right_doc != nullptr && right_bounds.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Right;
            m_completion_popup.hide();
            m_signature_help.hide();
            m_focused = true;

            const float right_gutter_w = layout.gutter_bounds.width;
            const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
            const float fold_margin_left = splitter_x + 2.0F * scale + right_gutter_w - fold_margin;
            if (point_x >= fold_margin_left && point_x <= splitter_x + 2.0F * scale + right_gutter_w)
            {
                const float line_height = 20.0F * scale;
                const std::size_t visible_count = static_cast<std::size_t>(std::max(
                    static_cast<int>(layout.editor_bounds.height / line_height), 1));
                const std::size_t split_total_lines = right_doc->get_line_count();
                const std::size_t clicked_row = static_cast<std::size_t>(std::max(
                    static_cast<int>((point_y - layout.editor_bounds.y) / line_height), 0));
                const std::size_t split_line = visual_row_to_physical_line(
                    m_split_folding, m_split_scrollbar.get_first_visible_line() + clicked_row, split_total_lines);

                if (m_split_folding.is_fold_start(split_line))
                {
                    m_split_folding.toggle_fold(split_line);
                    m_split_scrollbar.synchronize(count_visible_lines(m_split_folding, split_total_lines), visible_count);
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                    if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
                    return true;
                }
            }

            m_pointer_selecting = true;
            const UI::Editor::TextPosition pos = position_from_point(surface, device_context, layout, point_x, point_y);
            static_cast<void>(right_doc->set_caret(pos.line, pos.column, extend_selection));
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            return true;
        }

        // Left Editor Pane (Code or Gutter)
        if (left_doc != nullptr && left_bounds.contains(point_x, point_y))
        {
            m_focused_pane = SplitPaneFocus::Left;
            m_completion_popup.hide();
            m_signature_help.hide();
            m_focused = true;

            const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
            const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
            if (point_x >= fold_margin_left && point_x <= layout.gutter_bounds.right())
            {
                const float line_height = 20.0F * scale;
                const std::size_t visible_count = static_cast<std::size_t>(std::max(
                    static_cast<int>(layout.editor_bounds.height / line_height), 1));
                const std::size_t total_lines = left_doc->get_line_count();
                const std::size_t clicked_row = static_cast<std::size_t>(std::max(
                    static_cast<int>((point_y - layout.editor_bounds.y) / line_height), 0));
                const std::size_t line_index = visual_row_to_physical_line(
                    m_folding, m_scrollbar.get_first_visible_line() + clicked_row, total_lines);

                if (m_folding.is_fold_start(line_index))
                {
                    m_folding.toggle_fold(line_index);
                    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
                    m_reveal_caret_pending = true;
                    m_caret_blink.reset();
                    if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
                    return true;
                }
            }

            m_pointer_selecting = true;
            const UI::Editor::TextPosition pos = position_from_point(surface, device_context, layout, point_x, point_y);
            static_cast<void>(left_doc->set_caret(pos.line, pos.column, extend_selection));
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            return true;
        }
    }

    document = m_controller.get_active_document();
    if (document != nullptr && m_minimap.is_point(layout, point_x, point_y))
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_scrollbar.synchronize(document->get_line_count(), visible_count);
        const std::optional<std::size_t> target = m_minimap.handle_pointer_press(
            layout,
            point_x,
            point_y,
            document->get_line_count(),
            visible_count,
            m_scrollbar.get_first_visible_line());
        if (target)
        {
            static_cast<void>(m_scrollbar.scroll_to(*target));
        }
        m_focused = true;
        m_pointer_selecting = false;
        m_reveal_caret_pending = false;
        m_caret_blink.reset();
        return true;
    }
    if (document != nullptr && m_scrollbar.is_point(layout, point_x, point_y))
    {
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

    if (document == nullptr)
    {
        if (layout.editor_bounds.contains(point_x, point_y))
        {
            const float dpi = surface.m_dpi_scale;
            const int center_x = round_to_int(layout.editor_bounds.x + layout.editor_bounds.width * 0.5F);
            
            const int logo_size = round_to_int(150.0F * dpi);
            const int gap1 = round_to_int(30.0F * dpi);
            const int gap2 = round_to_int(40.0F * dpi);
            const int total_height = logo_size + gap1 + gap2 + 3 * round_to_int(28.0F * dpi);
            const float full_height = layout.editor_bounds.height + layout.terminal_panel_bounds.height;
            int current_y = round_to_int(layout.editor_bounds.y + (full_height - total_height) * 0.5F);
            current_y += logo_size + gap1;
            current_y += gap2;

            const float btn_w = 300.0F * dpi;
            const float btn_h = 40.0F * dpi;
            const float btn_x = center_x - btn_w * 0.5F;

            m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
            current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);
            m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});

            if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y))
            {
                command_out = "zde.project.open";
                return true;
            }
            if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y))
            {
                command_out = "zde.git.clone";
                return true;
            }
        }
    }

    if ((!layout.gutter_bounds.contains(point_x, point_y) &&
         !layout.editor_bounds.contains(point_x, point_y)) ||
        document == nullptr)
    {
        return false;
    }

    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);

    if (const std::optional<std::size_t> fold_line = fold_start_line_at_point(
            m_folding, layout, point_x, point_y, surface.m_dpi_scale,
            m_scrollbar.get_first_visible_line(), total_lines))
    {
        m_folding.toggle_fold(*fold_line);
        m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
        m_reveal_caret_pending = true;
        return true;
    }

    m_completion_popup.hide();
    m_signature_help.hide();
    m_focused = true;
    m_pointer_selecting = true;
    const UI::Editor::TextPosition position = position_from_point(
        surface, device_context, layout, point_x, point_y);
    static_cast<void>(document->set_caret(
        position.line, position.column, extend_selection));
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    return true;
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    const bool scrollbar_changed = m_scrollbar.set_hovered(layout, point_x, point_y);
    
    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        bool changed = false;
        changed |= m_empty_state_open_btn.handle_pointer_move(point_x, point_y);
        changed |= m_empty_state_clone_btn.handle_pointer_move(point_x, point_y);
        if (changed)
            return true;
    }

    std::optional<std::size_t> hovered_fold_line;
    if (document != nullptr)
    {
        const std::size_t total_lines = document->get_line_count();
        const float line_height = 20.0F * layout.dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
        hovered_fold_line = fold_start_line_at_point(
            m_folding, layout, point_x, point_y, layout.dpi_scale,
            m_scrollbar.get_first_visible_line(), total_lines);
    }
    
    if (hovered_fold_line != m_hovered_fold_line)
    {
        m_hovered_fold_line = hovered_fold_line;
        return true;
    }

    std::optional<std::size_t> hovered_tab;
    std::optional<std::size_t> hovered_close;
    const float close_width =
        UI::Editor::StudioEditorMetrics::editor_tab_close_width * layout.dpi_scale;
    for (std::size_t index = 0; index < m_tab_count; ++index)
    {
        const UI::Rect& tab_bounds = m_tab_bounds[index];
        const UI::Rect close_bounds{
            tab_bounds.right() - close_width,
            tab_bounds.y,
            close_width,
            tab_bounds.height};
        if (tab_bounds.contains(point_x, point_y))
        {
            hovered_tab = index;
            if (close_bounds.contains(point_x, point_y))
            {
                hovered_close = index;
            }
            break;
        }
    }
    bool hovered_tab_scrollbar = false;
    if (m_max_tab_scroll > 0.0f)
    {
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
        hovered_tab_scrollbar = hit_bounds.contains(point_x, point_y);
    }
    bool changed = false;
    if (hovered_tab_scrollbar != m_hovered_tab_scrollbar)
    {
        m_hovered_tab_scrollbar = hovered_tab_scrollbar;
        changed = true;
    }

    bool action_menu_changed = false;
    if (m_tab_action_menu.visible)
    {
        std::optional<std::size_t> next_hover;
        for (std::size_t i = 0; i < m_tab_action_menu.item_bounds.size(); ++i)
        {
            if (!m_tab_action_menu.items[i].is_separator &&
                m_tab_action_menu.item_bounds[i].contains(point_x, point_y))
            {
                next_hover = i;
                break;
            }
        }
        if (next_hover != m_tab_action_menu.hovered_index)
        {
            m_tab_action_menu.hovered_index = next_hover;
            action_menu_changed = true;
        }
    }

    std::optional<std::size_t> next_tab_action;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (m_tab_action_bounds[i].contains(point_x, point_y))
        {
            next_tab_action = i;
            break;
        }
    }
    if (next_tab_action != m_hovered_tab_action)
    {
        m_hovered_tab_action = next_tab_action;
        action_menu_changed = true;
    }

    bool split_close_hover = m_is_split && m_split_close_btn_bounds.contains(point_x, point_y);
    if (split_close_hover != m_hovered_split_close)
    {
        m_hovered_split_close = split_close_hover;
        changed = true;
    }

    bool split_resize_hover = is_split_resize_handle_point(layout, point_x, point_y);
    if (split_resize_hover != m_hovered_split_resize)
    {
        m_hovered_split_resize = split_resize_hover;
        changed = true;
    }

    std::optional<HoveredDiagnosticInfo> hovered_diag;
    if (document != nullptr && layout.editor_bounds.contains(point_x, point_y))
    {
        const float scale = layout.dpi_scale;
        const float line_height = 20.0F * scale;
        const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

        const bool is_right_pane = is_split_active && (point_x > splitter_x);
        const UI::Editor::TextDocumentModel* target_doc = is_right_pane ? m_controller.get_document(*m_split_document_index) : document;
        const auto& target_folding = is_right_pane ? m_split_folding : m_folding;
        const auto& target_scrollbar = is_right_pane ? m_split_scrollbar : m_scrollbar;

        if (target_doc != nullptr)
        {
            const std::size_t total_lines = target_doc->get_line_count();
            const std::size_t first_line = target_scrollbar.get_first_visible_line();
            const float clamped_y = std::clamp(point_y, layout.editor_bounds.y, layout.editor_bounds.bottom() - 1.0F);
            const std::size_t row = static_cast<std::size_t>(std::max(static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
            const std::size_t line_index = visual_row_to_physical_line(target_folding, first_line + row, total_lines);

            if (line_index < total_lines)
            {
                const std::string_view line_str = target_doc->get_line(line_index);
                const auto diags = target_doc->get_diagnostics_for_line(line_index);
                if (!diags.empty())
                {
                    const float pane_left = is_right_pane ? (splitter_x + 2.0F * scale) : layout.editor_bounds.x;
                    const float code_x = pane_left + layout.gutter_bounds.width + 14.0F * scale;
                    const float line_top_y = layout.editor_bounds.y + static_cast<float>(row) * line_height;
                    const float line_bottom_y = line_top_y + line_height;

                    if (point_y >= line_top_y - 2.0F * scale && point_y <= line_bottom_y + 4.0F * scale && point_x >= code_x - 10.0F * scale)
                    {
                        for (const auto& d : diags)
                        {
                            HoveredDiagnosticInfo info;
                            info.diagnostic = d;
                            info.anchor_x = point_x;
                            info.anchor_y = line_top_y;
                            info.line_text = std::string(line_str);

                            std::string token_symbol;
                            auto open_angle = line_str.find('<');
                            auto close_angle = line_str.find('>');
                            if (open_angle != std::string_view::npos && close_angle != std::string_view::npos && close_angle > open_angle)
                            {
                                token_symbol = std::string(line_str.substr(open_angle + 1, close_angle - open_angle - 1));
                            }
                            else
                            {
                                auto open_q = line_str.find('"');
                                auto close_q = line_str.rfind('"');
                                if (open_q != std::string_view::npos && close_q != std::string_view::npos && close_q > open_q)
                                {
                                    token_symbol = std::string(line_str.substr(open_q + 1, close_q - open_q - 1));
                                }
                            }
                            info.symbol_name = token_symbol;
                            hovered_diag = info;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (hovered_diag.has_value() != m_hovered_diagnostic.has_value() ||
        (hovered_diag.has_value() && m_hovered_diagnostic.has_value() &&
         (hovered_diag->diagnostic.message != m_hovered_diagnostic->diagnostic.message ||
          hovered_diag->anchor_y != m_hovered_diagnostic->anchor_y)))
    {
        m_hovered_diagnostic = hovered_diag;
        changed = true;
    }

    if (hovered_tab != m_hovered_tab_index || hovered_close != m_hovered_tab_close_index)
    {
        m_hovered_tab_index = hovered_tab;
        m_hovered_tab_close_index = hovered_close;
        changed = true;
    }
    return scrollbar_changed || changed || action_menu_changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y)
{
    if (m_is_resizing_split)
    {
        const float rel_x = point_x - layout.editor_bounds.x;
        m_split_ratio = std::clamp(rel_x / layout.editor_bounds.width, 0.15F, 0.85F);
        if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        return true;
    }

    if (m_dragging_tab_scrollbar)
    {
        const float track_width = layout.tab_bar_bounds.width;
        const float thumb_width = std::max(
            20.0F * layout.dpi_scale,
            track_width * (track_width / (track_width + m_max_tab_scroll)));
        const float track_space = track_width - thumb_width;
        
        if (track_space > 0.0f)
        {
            const float delta_x = point_x - m_tab_scroll_drag_start_x;
            const float scroll_delta = (delta_x / track_space) * m_max_tab_scroll;
            m_tab_scroll_offset = m_tab_scroll_drag_initial_offset + scroll_delta;
            m_tab_scroll_offset = std::clamp(m_tab_scroll_offset, 0.0f, m_max_tab_scroll);
        }
        return true;
    }

    if (m_tab_drag_drop.is_dragging())
    {
        static_cast<void>(m_tab_drag_drop.drag(point_x));
        m_drag_cursor_x = point_x;
        m_drag_cursor_y = point_y;

        // If dragged into editor area (below tab bar)
        if (point_y > layout.tab_bar_bounds.bottom())
        {
            const float ed_x = layout.editor_bounds.x;
            const float ed_w = layout.editor_bounds.width;
            const float ed_y = layout.editor_bounds.y;
            const float ed_h = layout.editor_bounds.height;

            if (point_x > ed_x + ed_w * 0.60F)
            {
                m_active_drop_zone = SplitDropZone::Right;
            }
            else if (point_x < ed_x + ed_w * 0.40F)
            {
                m_active_drop_zone = SplitDropZone::Left;
            }
            else if (point_y > ed_y + ed_h * 0.65F)
            {
                m_active_drop_zone = SplitDropZone::Bottom;
            }
            else if (point_y < ed_y + ed_h * 0.35F)
            {
                m_active_drop_zone = SplitDropZone::Top;
            }
            else
            {
                m_active_drop_zone = SplitDropZone::Center;
            }
            if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
            return true;
        }

        m_active_drop_zone = SplitDropZone::None;
        float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
        const std::span<const UI::Editor::EditorSessionDocument> documents =
            m_controller.get_documents();
        for (std::size_t index = 0; index < documents.size(); ++index)
        {
            const float width = UI::Editor::calculate_editor_tab_width(
                static_cast<float>(surface.get_text_width(
                    device_context,
                    *surface.m_ui_font,
                    documents[index].text.get_file_name())),
                surface.m_dpi_scale);
            const UI::Rect bounds{
                tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
            if (bounds.contains(point_x, layout.tab_bar_bounds.y))
            {
                if (m_tab_drag_drop.get_dragged_index() != index)
                {
                    static_cast<void>(
                        m_controller.reorder_file(m_tab_drag_drop.get_dragged_index(), index));
                    m_tab_drag_drop.update_dragged_index(index);
                }
                break;
            }
            tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
        }
        if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        return true;
    }

    const float scale = surface.m_dpi_scale;
    const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
    const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    if (is_split_active)
    {
        const float scroll_top_y = layout.editor_bounds.y;
        const float scroll_total_h = layout.editor_bounds.height;
        const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
        const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
        const float left_minimap_w = (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y, left_minimap_w, scroll_total_h};

        const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
        const float right_minimap_w = (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w, scroll_top_y, right_minimap_w, scroll_total_h};

        const float line_height = 20.0F * scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));

        if (auto* left_doc = m_controller.get_active_document())
        {
            UI::Editor::StudioEditorLayoutResult llay = layout;
            llay.minimap_bounds = left_minimap;
            llay.scrollbar_bounds = left_scrollbar;
            const auto target = m_minimap.handle_pointer_drag(llay, point_y, left_doc->get_line_count(), visible_count, m_scrollbar.get_first_visible_line());
            if (target) { static_cast<void>(m_scrollbar.scroll_to(*target)); m_reveal_caret_pending = false; return true; }
            if (m_scrollbar.handle_pointer_drag(llay, point_y)) { m_reveal_caret_pending = false; return true; }
        }

        if (auto* right_doc = m_controller.get_document(*m_split_document_index))
        {
            UI::Editor::StudioEditorLayoutResult rlay = layout;
            rlay.minimap_bounds = right_minimap;
            rlay.scrollbar_bounds = right_scrollbar;
            const auto target = m_split_minimap.handle_pointer_drag(rlay, point_y, right_doc->get_line_count(), visible_count, m_split_scrollbar.get_first_visible_line());
            if (target) { static_cast<void>(m_split_scrollbar.scroll_to(*target)); m_reveal_caret_pending = false; return true; }
            if (m_split_scrollbar.handle_pointer_drag(rlay, point_y)) { m_reveal_caret_pending = false; return true; }
        }

        if (m_pointer_selecting)
        {
            if (m_focused_pane == SplitPaneFocus::Right)
            {
                if (auto* right_doc = m_controller.get_document(*m_split_document_index))
                {
                    const UI::Editor::TextPosition pos = position_from_point(surface, device_context, layout, point_x, point_y);
                    const bool chg = right_doc->set_caret(pos.line, pos.column, true);
                    if (chg) { m_reveal_caret_pending = true; m_caret_blink.reset(); }
                    return chg;
                }
            }
            else
            {
                if (auto* left_doc = m_controller.get_active_document())
                {
                    const UI::Editor::TextPosition pos = position_from_point(surface, device_context, layout, point_x, point_y);
                    const bool chg = left_doc->set_caret(pos.line, pos.column, true);
                    if (chg) { m_reveal_caret_pending = true; m_caret_blink.reset(); }
                    return chg;
                }
            }
        }
        return false;
    }

    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document != nullptr)
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        const std::optional<std::size_t> target = m_minimap.handle_pointer_drag(
            layout,
            point_y,
            document->get_line_count(),
            visible_count,
            m_scrollbar.get_first_visible_line());
        if (target)
        {
            static_cast<void>(m_scrollbar.scroll_to(*target));
            m_reveal_caret_pending = false;
            return true;
        }
    }
    if (m_scrollbar.handle_pointer_drag(layout, point_y))
    {
        m_reveal_caret_pending = false;
        return true;
    }
    if (!m_pointer_selecting || document == nullptr)
    {
        return false;
    }
    const UI::Editor::TextPosition position = position_from_point(
        surface, device_context, layout, point_x, point_y);
    const bool changed = document->set_caret(position.line, position.column, true);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
    }
    return changed;
}

bool TextEditor::handle_pointer_release() noexcept
{
    if (m_is_resizing_split)
    {
        m_is_resizing_split = false;
        if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        return true;
    }

    if (m_dragging_tab_scrollbar)
    {
        m_dragging_tab_scrollbar = false;
        return true;
    }

    if (m_tab_drag_drop.is_dragging())
    {
        if (m_active_drop_zone == SplitDropZone::Right || m_active_drop_zone == SplitDropZone::Left || m_active_drop_zone == SplitDropZone::Center)
        {
            const std::size_t dragged_idx = m_tab_drag_drop.get_dragged_index();
            m_is_split = true;
            m_split_document_index = dragged_idx;

            if (m_controller.get_active_index() == dragged_idx && m_controller.get_documents().size() > 1)
            {
                const std::size_t other_idx = (dragged_idx == 0) ? 1 : 0;
                static_cast<void>(m_controller.activate_file(other_idx));
            }
            std::clog << "[ZDE] Split Editor activated via drag-and-drop with file index: " << dragged_idx << "\n";
        }
        m_active_drop_zone = SplitDropZone::None;
        m_tab_drag_drop.end_drag();
        if (m_window_handle) InvalidateRect(m_window_handle, nullptr, FALSE);
        return true;
    }

    const bool was_selecting = m_pointer_selecting;
    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        m_empty_state_open_btn.set_pressed(false);
        m_empty_state_clone_btn.set_pressed(false);
    }
    m_pointer_selecting = false;
    const bool minimap_was_dragging = m_minimap.handle_pointer_release() || m_split_minimap.handle_pointer_release();
    const bool scrollbar_was_dragging = m_scrollbar.handle_pointer_release() || m_split_scrollbar.handle_pointer_release();
    return minimap_was_dragging || scrollbar_was_dragging || was_selecting;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    const Event::ScrollEvent& event) noexcept
{
    const float speed = 32.0f * layout.dpi_scale;

    // Tab bar scroll isolation: horizontal or vertical mouse wheel over tabs scrolls tab bar only
    if (layout.tab_bar_bounds.contains(event.point_x, event.point_y))
    {
        const float delta = event.delta_x != 0 ? static_cast<float>(event.delta_x)
                                               : static_cast<float>(event.delta_y);
        if (delta != 0.0f)
        {
            m_tab_scroll_offset += delta * speed;
            if (m_tab_scroll_offset < 0.0f) m_tab_scroll_offset = 0.0f;
            if (m_tab_scroll_offset > m_max_tab_scroll) m_tab_scroll_offset = m_max_tab_scroll;
            return true;
        }
        return false;
    }

    // Horizontal scroll in editor area
    if (event.delta_x != 0)
    {
        if (layout.editor_bounds.contains(event.point_x, event.point_y))
        {
            m_text_scroll_offset += static_cast<float>(event.delta_x) * speed;
            if (m_text_scroll_offset < 0.0f) m_text_scroll_offset = 0.0f;
            if (m_text_scroll_offset > m_max_text_scroll) m_text_scroll_offset = m_max_text_scroll;
            return true;
        }
        return false;
    }

    if (event.delta_y == 0) return false;

    // Intercept scroll if pointer is over completion popup
    if (m_completion_popup.is_visible() &&
        m_completion_popup.is_point_inside(event.point_x, event.point_y, 24.0F * surface.m_dpi_scale, 340.0F * surface.m_dpi_scale))
    {
        return m_completion_popup.scroll(event.delta_y);
    }

    // Vertical scroll in editor area only
    if (layout.editor_bounds.contains(event.point_x, event.point_y) ||
        layout.gutter_bounds.contains(event.point_x, event.point_y) ||
        layout.minimap_bounds.contains(event.point_x, event.point_y) ||
        layout.scrollbar_bounds.contains(event.point_x, event.point_y) ||
        m_scrollbar.is_point(layout, event.point_x, event.point_y) ||
        is_scrollbar_point(layout, event.point_x, event.point_y) ||
        is_minimap_point(layout, event.point_x, event.point_y))
    {
        const float scale = surface.m_dpi_scale;
        const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

        if (is_split_active && event.point_x > splitter_x)
        {
            if (const UI::Editor::TextDocumentModel* split_doc = m_controller.get_document(*m_split_document_index))
            {
                const float line_height = 20.0F * scale;
                const std::size_t visible_count = static_cast<std::size_t>(std::max(
                    static_cast<int>(layout.editor_bounds.height / line_height), 1));
                m_split_scrollbar.synchronize(split_doc->get_line_count(), visible_count);
                return m_split_scrollbar.scroll_lines(event.delta_y);
            }
        }
        else
        {
            if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
            {
                const float line_height = 20.0F * scale;
                const std::size_t visible_count = static_cast<std::size_t>(std::max(
                    static_cast<int>(layout.editor_bounds.height / line_height), 1));
                m_scrollbar.synchronize(document->get_line_count(), visible_count);
            }
            m_reveal_caret_pending = false;
            return m_scrollbar.scroll_lines(event.delta_y);
        }
    }

    return false;
}

bool TextEditor::handle_input(
    UI::Editor::EditorInputCommand command,
    bool extend_selection)
{
    {
        std::lock_guard<std::mutex> lock(m_lsp_mutex);
        if (m_completion_popup.is_visible())
        {
            if (command == UI::Editor::EditorInputCommand::MoveUp)
            {
                m_completion_popup.select_previous();
                return true;
            }
            if (command == UI::Editor::EditorInputCommand::MoveDown)
            {
                m_completion_popup.select_next();
                return true;
            }
            if (command == UI::Editor::EditorInputCommand::Escape ||
                command == UI::Editor::EditorInputCommand::MoveLeft ||
                command == UI::Editor::EditorInputCommand::MoveRight ||
                command == UI::Editor::EditorInputCommand::MoveHome ||
                command == UI::Editor::EditorInputCommand::MoveEnd)
            {
                m_completion_popup.hide();
                m_signature_help.hide();
                if (command == UI::Editor::EditorInputCommand::Escape)
                {
                    return true;
                }
            }
            if (command == UI::Editor::EditorInputCommand::InsertTab || command == UI::Editor::EditorInputCommand::InsertNewLine)
            {
                if (const auto* item = m_completion_popup.get_selected_item())
                {
                    if (auto* doc = m_controller.get_active_document(); doc != nullptr)
                    {
                        const std::string_view current_line = doc->get_line(doc->get_caret_line());
                        const std::size_t caret_col = doc->get_caret_column();

                        // Find how many characters of the current token to replace before caret
                        std::size_t word_start = std::min(caret_col, current_line.size());
                        while (word_start > 0)
                        {
                            const char c = current_line[word_start - 1];
                            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#' || c == '~')
                            {
                                --word_start;
                            }
                            else
                            {
                                break;
                            }
                        }

                        const std::size_t prefix_len = caret_col - word_start;
                        for (std::size_t k = 0; k < prefix_len; ++k)
                        {
                            static_cast<void>(m_controller.execute_input(UI::Editor::EditorInputCommand::DeleteBackward));
                        }

                        std::string text_to_insert = item->insert_text;
                        const std::size_t start_line = doc->get_caret_line();
                        const std::size_t start_col = doc->get_caret_column();

                        std::size_t target_cursor_line = start_line;
                        std::size_t target_cursor_col = start_col;
                        bool cursor_targeted = false;

                        // 1. Find all placeholders ${N:default_val} and replace all instances
                        std::size_t first_placeholder_pos = std::string::npos;
                        while (true)
                        {
                            const std::size_t p_start = text_to_insert.find("${");
                            if (p_start == std::string::npos) break;
                            const std::size_t p_end = text_to_insert.find('}', p_start);
                            if (p_end == std::string::npos) break;

                            const std::string inner = text_to_insert.substr(p_start + 2, p_end - (p_start + 2));
                            std::string def_val;
                            const std::string tag = "${" + inner + "}";
                            const std::size_t colon = inner.find(':');
                            if (colon != std::string::npos)
                            {
                                def_val = inner.substr(colon + 1);
                            }

                            if (first_placeholder_pos == std::string::npos)
                            {
                                first_placeholder_pos = p_start;
                            }

                            // Replace all occurrences of this exact tag
                            std::size_t search_pos = 0;
                            while ((search_pos = text_to_insert.find(tag, search_pos)) != std::string::npos)
                            {
                                text_to_insert.replace(search_pos, tag.size(), def_val);
                                search_pos += def_val.size();
                            }
                        }

                        // 2. Handle $0 tabstop
                        const std::size_t tabstop_pos = text_to_insert.find("$0");
                        if (tabstop_pos != std::string::npos)
                        {
                            std::size_t line_offset = 0;
                            std::size_t last_nl = 0;
                            for (std::size_t idx = 0; idx < tabstop_pos; ++idx)
                            {
                                if (text_to_insert[idx] == '\n')
                                {
                                    ++line_offset;
                                    last_nl = idx + 1;
                                }
                            }
                            const std::size_t col_offset = tabstop_pos - last_nl;
                            target_cursor_line = start_line + line_offset;
                            target_cursor_col = (line_offset == 0) ? start_col + col_offset : col_offset;
                            cursor_targeted = true;

                            text_to_insert.erase(tabstop_pos, 2);
                        }
                        else if (first_placeholder_pos != std::string::npos)
                        {
                            std::size_t line_offset = 0;
                            std::size_t last_nl = 0;
                            for (std::size_t idx = 0; idx < first_placeholder_pos; ++idx)
                            {
                                if (text_to_insert[idx] == '\n')
                                {
                                    ++line_offset;
                                    last_nl = idx + 1;
                                }
                            }
                            const std::size_t col_offset = first_placeholder_pos - last_nl;
                            target_cursor_line = start_line + line_offset;
                            target_cursor_col = (line_offset == 0) ? start_col + col_offset : col_offset;
                            cursor_targeted = true;
                        }

                        static_cast<void>(m_controller.insert_text(text_to_insert));

                        if (cursor_targeted)
                        {
                            const std::size_t max_lines = doc->get_line_count();
                            if (target_cursor_line < max_lines)
                            {
                                const std::size_t line_len = doc->get_line(target_cursor_line).size();
                                doc->set_caret(target_cursor_line, std::min(target_cursor_col, line_len));
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

    const bool changed = m_focused && m_controller.execute_input(command, extend_selection);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();

        if (auto* doc = m_controller.get_active_document(); doc != nullptr)
        {
            const std::string uri = get_active_document_uri();
            const std::string fname = get_active_document_filename();
            std::string content;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
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
                command == UI::Editor::EditorInputCommand::Escape)
            {
                m_signature_help.hide();
            }
            else if (m_signature_help.is_visible())
            {
                const std::string_view current_line = doc->get_line(doc->get_caret_line());
                const std::size_t caret_col = doc->get_caret_column();
                const std::string_view prefix = current_line.substr(0, std::min(caret_col, current_line.size()));

                std::size_t open_count = 0;
                std::size_t close_count = 0;
                for (char ch : prefix)
                {
                    if (ch == '(') ++open_count;
                    else if (ch == ')') ++close_count;
                }

                if (open_count <= close_count)
                {
                    m_signature_help.hide();
                }
            }

            // If Backspace or Delete occurred while completion popup was open, auto-close or re-filter
            if (m_completion_popup.is_visible())
            {
                const std::string_view current_line = doc->get_line(doc->get_caret_line());
                const std::size_t caret_col = doc->get_caret_column();

                std::size_t word_start = std::min(caret_col, current_line.size());
                while (word_start > 0)
                {
                    const char c = current_line[word_start - 1];
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#' || c == '~')
                    {
                        --word_start;
                    }
                    else
                    {
                        break;
                    }
                }
                const std::string_view current_word = current_line.substr(word_start, caret_col - word_start);

                if (current_word.empty() || current_word == "#" || caret_col == 0)
                {
                    m_completion_popup.hide();
                }
                else
                {
                    m_completion_popup.set_filter(current_word);
                    if (m_completion_popup.get_item_count() == 0)
                    {
                        m_completion_popup.hide();
                    }
                }
            }
        }
    }

    if (m_is_split && m_focused_pane == SplitPaneFocus::Right && m_split_document_index.has_value())
    {
        if (auto* right_doc = m_controller.get_document(*m_split_document_index))
        {
            const bool changed = right_doc->execute(command, extend_selection);
            if (changed)
            {
                m_reveal_caret_pending = true;
                m_caret_blink.reset();
            }
            return changed;
        }
    }

    return changed;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action)
{
    if (m_is_split && m_focused_pane == SplitPaneFocus::Right && m_split_document_index.has_value())
    {
        if (auto* right_doc = m_controller.get_document(*m_split_document_index))
        {
            bool changed = false;
            switch (action)
            {
            case UI::Editor::EditorAction::SelectAll: changed = right_doc->select_all(); break;
            case UI::Editor::EditorAction::ToggleComment: changed = right_doc->toggle_line_comment(); break;
            case UI::Editor::EditorAction::MoveLineUp: changed = right_doc->move_line_up(); break;
            case UI::Editor::EditorAction::MoveLineDown: changed = right_doc->move_line_down(); break;
            case UI::Editor::EditorAction::AddCursorAbove: changed = right_doc->add_cursor_above(); break;
            case UI::Editor::EditorAction::AddCursorBelow: changed = right_doc->add_cursor_below(); break;
            default: break;
            }
            if (changed)
            {
                m_reveal_caret_pending = true;
                m_caret_blink.reset();
            }
            return changed;
        }
    }

    const bool changed = m_controller.execute_action(action);
    if (changed)
    {
        if (action == UI::Editor::EditorAction::CreateDocument ||
            action == UI::Editor::EditorAction::CloseDocument ||
            action == UI::Editor::EditorAction::RemoveDocument)
        {
            m_scrollbar.reset();
        }
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        if (action == UI::Editor::EditorAction::CreateDocument ||
            action == UI::Editor::EditorAction::CloseDocument ||
            action == UI::Editor::EditorAction::RemoveDocument)
        {
            m_hovered_tab_index.reset();
            m_hovered_tab_close_index.reset();
        }
    }
    return changed;
}

void TextEditor::reset_split() noexcept
{
    m_is_split = false;
    m_split_document_index.reset();
    m_focused_pane = SplitPaneFocus::Left;
    m_scrollbar.reset();
    m_split_scrollbar.reset();
}

std::optional<bool> TextEditor::handle_command(std::string_view command_id)
{
    if (command_id == Commands::CommandIds::view_split_right || command_id == "zde.editor.split_right" ||
        command_id == Commands::CommandIds::view_split_left || command_id == Commands::CommandIds::view_split_up ||
        command_id == Commands::CommandIds::view_split_down)
    {
        m_is_split = !m_is_split;
        if (m_is_split)
        {
            const auto doc_count = m_controller.get_documents().size();
            if (doc_count > 1)
            {
                const std::size_t active = m_controller.get_active_index().value_or(0);
                m_split_document_index = (active + 1) % doc_count;
            }
            else
            {
                m_split_document_index = m_controller.get_active_index().value_or(0);
            }
            std::clog << "[ZDE] Split Editor activated\n";
        }
        else
        {
            m_split_document_index.reset();
            std::clog << "[ZDE] Split Editor closed\n";
        }
        return true;
    }

    if (command_id == Commands::CommandIds::window_next_tab || command_id == "zde.editor.nextTab")
    {
        const auto doc_count = m_controller.get_documents().size();
        if (doc_count > 1)
        {
            const std::size_t active = m_controller.get_active_index().value_or(0);
            const std::size_t next = (active + 1) % doc_count;
            static_cast<void>(m_controller.activate_file(next));
            m_scrollbar.reset();
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            return true;
        }
        return false;
    }

    if (command_id == Commands::CommandIds::window_prev_tab || command_id == "zde.editor.prevTab")
    {
        const auto doc_count = m_controller.get_documents().size();
        if (doc_count > 1)
        {
            const std::size_t active = m_controller.get_active_index().value_or(0);
            const std::size_t prev = (active == 0) ? (doc_count - 1) : (active - 1);
            static_cast<void>(m_controller.activate_file(prev));
            m_scrollbar.reset();
            m_reveal_caret_pending = true;
            m_caret_blink.reset();
            return true;
        }
        return false;
    }

    if (command_id == Commands::CommandIds::file_close_all || command_id == "zde.editor.closeAll" ||
        command_id == "workbench.action.closeAllEditors")
    {
        return close_all_files();
    }

    if (command_id == "workbench.action.closeActiveEditor" || command_id == Commands::CommandIds::file_close)
    {
        return handle_action(UI::Editor::EditorAction::CloseDocument);
    }

    if (command_id == "zde.editor.focus_first_group")
    {
        m_focused_pane = SplitPaneFocus::Left;
        return true;
    }
    if (command_id == "zde.editor.focus_second_group")
    {
        if (m_is_split)
        {
            m_focused_pane = SplitPaneFocus::Right;
            return true;
        }
        return false;
    }

    const std::optional<UI::Editor::EditorAction> action =
        UI::Editor::EditorController::action_from_command_id(command_id);
    return action ? std::optional<bool>{handle_action(*action)} : std::nullopt;
}

std::optional<bool> TextEditor::is_command_enabled(
    std::string_view command_id) const noexcept
{
    if (command_id == Commands::CommandIds::view_split_right || command_id == "zde.editor.split_right" ||
        command_id == Commands::CommandIds::view_split_left || command_id == Commands::CommandIds::view_split_up ||
        command_id == Commands::CommandIds::view_split_down ||
        command_id == Commands::CommandIds::window_next_tab || command_id == "zde.editor.nextTab" ||
        command_id == Commands::CommandIds::window_prev_tab || command_id == "zde.editor.prevTab" ||
        command_id == Commands::CommandIds::file_close_all || command_id == "zde.editor.closeAll" ||
        command_id == "workbench.action.closeActiveEditor" || command_id == "zde.editor.focus_first_group" ||
        command_id == "zde.editor.focus_second_group")
    {
        return true;
    }
    const std::optional<UI::Editor::EditorAction> action =
        UI::Editor::EditorController::action_from_command_id(command_id);
    return action ? std::optional<bool>{m_controller.can_execute_action(*action)} : std::nullopt;
}

bool TextEditor::handle_text_input(std::string_view utf8_text)
{
    if (!m_focused)
    {
        return false;
    }

    auto* doc = get_focused_document();
    if (doc == nullptr)
    {
        return false;
    }

    const std::size_t initial_caret_line = doc->get_caret_line();
    const std::size_t initial_caret_col = doc->get_caret_column();
    const std::string_view initial_line = doc->get_line(initial_caret_line);

    // 1. Skip-over existing closing character when typed
    if ((utf8_text == ")" || utf8_text == "]" || utf8_text == "}" || utf8_text == "\"" || utf8_text == "'") &&
        initial_caret_col < initial_line.size() && initial_line[initial_caret_col] == utf8_text[0])
    {
        doc->set_caret(initial_caret_line, initial_caret_col + 1);
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
        m_completion_popup.hide();
        m_signature_help.hide();
        return true;
    }

    // 2. Smart auto-closing for (, [, ", '
    if (utf8_text == "(" || utf8_text == "[" || utf8_text == "\"" || utf8_text == "'")
    {
        std::string pair_text;
        if (utf8_text == "(") pair_text = "()";
        else if (utf8_text == "[") pair_text = "[]";
        else if (utf8_text == "\"") pair_text = "\"\"";
        else if (utf8_text == "'") pair_text = "''";

        const bool changed = doc->insert_text(pair_text);
        if (changed)
        {
            doc->set_caret(initial_caret_line, initial_caret_col + 1);
            m_reveal_caret_pending = true;
            m_caret_blink.reset();

            const std::string fname = get_active_document_filename();
            const std::string uri = get_active_document_uri();
            std::string content;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                content += doc->get_line(i);
                content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_changed(
                uri, fname, 1, content);

            if (utf8_text == "(")
            {
                Language::Protocol::Position sig_pos{
                    .line = doc->get_caret_line(),
                    .character = doc->get_caret_column()
                };
                Language::LanguageServerManager::instance().request_signature_help(
                    uri, fname, sig_pos, doc->get_line(doc->get_caret_line()),
                    [this](std::optional<Language::Protocol::SignatureHelp> help) {
                        std::lock_guard<std::mutex> lock(m_lsp_mutex);
                        if (help.has_value() && !help->signatures.empty())
                        {
                            m_signature_help.show(std::move(*help), 0.0F, 0.0F);
                        }
                        else
                        {
                            m_signature_help.hide();
                        }
                        if (m_window_handle != nullptr)
                        {
                            InvalidateRect(m_window_handle, nullptr, FALSE);
                        }
                    }
                );
            }

            Language::Protocol::Position pos{
                .line = doc->get_caret_line(),
                .character = doc->get_caret_column()
            };
            Language::LanguageServerManager::instance().request_completion(
                uri, fname, pos, doc->get_line(doc->get_caret_line()),
                [this](std::vector<Language::Protocol::CompletionItem> items) {
                    std::lock_guard<std::mutex> lock(m_lsp_mutex);
                    if (!items.empty())
                    {
                        m_completion_popup.show(std::move(items), 100.0F, 100.0F);

                        if (const auto* current_doc = m_controller.get_active_document())
                        {
                            const std::string_view line = current_doc->get_line(current_doc->get_caret_line());
                            const std::size_t col = current_doc->get_caret_column();
                            std::size_t start = std::min(col, line.size());
                            while (start > 0)
                            {
                                const char ch = line[start - 1];
                                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '#' || ch == '~' || ch == '/' || ch == '.' || ch == '-')
                                {
                                    --start;
                                }
                                else
                                {
                                    break;
                                }
                            }
                            const std::string_view latest_word = line.substr(start, col - start);
                            if (!latest_word.empty())
                            {
                                m_completion_popup.set_filter(latest_word);
                            }
                        }

                        if (m_completion_popup.get_item_count() == 0)
                        {
                            m_completion_popup.hide();
                        }
                    }
                    if (m_window_handle != nullptr)
                    {
                        InvalidateRect(m_window_handle, nullptr, FALSE);
                    }
                }
            );
            return true;
        }
    }

    const bool changed = doc->insert_text(utf8_text);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();

        {
            const std::string fname = get_active_document_filename();
            const std::string uri = get_active_document_uri();
            std::string content;
            for (std::size_t i = 0; i < doc->get_line_count(); ++i)
            {
                content += doc->get_line(i);
                content += "\n";
            }
            Language::LanguageServerManager::instance().on_document_changed(
                uri, fname, 1, content);

            const std::string_view current_line = doc->get_line(doc->get_caret_line());
            const std::size_t caret_col = doc->get_caret_column();

            // Extract the active word token before cursor
            std::size_t word_start = std::min(caret_col, current_line.size());
            while (word_start > 0)
            {
                const char c = current_line[word_start - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#' || c == '~')
                {
                    --word_start;
                }
                else
                {
                    break;
                }
            }
            const std::string_view current_word = current_line.substr(word_start, caret_col - word_start);

            const bool is_include_context = current_line.find('#') != std::string_view::npos ||
                                           utf8_text == "<" || utf8_text == "\"" || utf8_text == "#";
            const bool is_trigger_char = utf8_text == "." || utf8_text == ">" || utf8_text == ":" ||
                                         utf8_text == "/" || utf8_text == "\\" || utf8_text == "(" ||
                                         utf8_text == "," || is_include_context ||
                                         current_word.size() >= 1;

            if (utf8_text == "{")
            {
                std::string_view line_before = current_line.substr(0, caret_col > 0 ? caret_col - 1 : 0);
                while (!line_before.empty() && std::isspace(static_cast<unsigned char>(line_before.back())))
                {
                    line_before.remove_suffix(1);
                }

                const bool is_type = line_before.find("struct ") != std::string_view::npos ||
                                     line_before.find("struct\t") != std::string_view::npos ||
                                     line_before.find("class ") != std::string_view::npos ||
                                     line_before.find("class\t") != std::string_view::npos ||
                                     line_before.find("enum ") != std::string_view::npos ||
                                     line_before.find("union ") != std::string_view::npos ||
                                     line_before.starts_with("struct") ||
                                     line_before.starts_with("class");
                const bool is_namespace = line_before.find("namespace ") != std::string_view::npos ||
                                         line_before.starts_with("namespace");

                if (is_type)
                {
                    static_cast<void>(m_controller.insert_text("\n    \n};"));
                    const std::size_t cur_line = doc->get_caret_line();
                    if (cur_line > 0)
                    {
                        doc->set_caret(cur_line - 1, 4);
                    }
                    std::lock_guard<std::mutex> lock(m_lsp_mutex);
                    m_completion_popup.hide();
                    return true;
                }
                else if (is_namespace)
                {
                    const std::size_t ns_pos = line_before.find("namespace");
                    std::string ns_name;
                    if (ns_pos != std::string_view::npos && ns_pos + 9 < line_before.size())
                    {
                        ns_name = std::string(line_before.substr(ns_pos + 9));
                        while (!ns_name.empty() && std::isspace(static_cast<unsigned char>(ns_name.front())))
                        {
                            ns_name.erase(0, 1);
                        }
                        while (!ns_name.empty() && std::isspace(static_cast<unsigned char>(ns_name.back())))
                        {
                            ns_name.pop_back();
                        }
                    }
                    const std::string closing = ns_name.empty() ? "\n\n\n}" : ("\n\n\n} // namespace " + ns_name);
                    static_cast<void>(m_controller.insert_text(closing));
                    const std::size_t cur_line = doc->get_caret_line();
                    if (cur_line >= 2)
                    {
                        doc->set_caret(cur_line - 2, 0);
                    }
                    std::lock_guard<std::mutex> lock(m_lsp_mutex);
                    m_completion_popup.hide();
                    return true;
                }
            }

            if (utf8_text == ">")
            {
                const std::filesystem::path cur_path(doc->get_file_name());
                const std::string cur_ext = cur_path.extension().string();
                const bool is_html_like = (cur_ext == ".html" || cur_ext == ".htm" || cur_ext == ".xhtml" ||
                                           cur_ext == ".jsx" || cur_ext == ".tsx");
                if (is_html_like && caret_col >= 2)
                {
                    const std::size_t open_pos = current_line.rfind('<', caret_col - 1);
                    if (open_pos != std::string_view::npos && open_pos + 1 < caret_col)
                    {
                        const char first_char = current_line[open_pos + 1];
                        if (first_char != '/' && first_char != '!' && first_char != '?')
                        {
                            std::string tag_name;
                            std::size_t idx = open_pos + 1;
                            while (idx < caret_col - 1 && !std::isspace(static_cast<unsigned char>(current_line[idx])) &&
                                   current_line[idx] != '/' && current_line[idx] != '>')
                            {
                                tag_name.push_back(current_line[idx]);
                                ++idx;
                            }

                            static const std::unordered_set<std::string> void_tags = {
                                "area", "base", "br", "col", "embed", "hr", "img", "input",
                                "link", "meta", "param", "source", "track", "wbr", "!doctype", "!DOCTYPE"
                            };

                            if (!tag_name.empty() && current_line[caret_col - 2] != '/' && !void_tags.contains(tag_name))
                            {
                                const std::string close_tag = "</" + tag_name + ">";
                                const std::size_t cur_line = doc->get_caret_line();
                                const std::size_t cur_col = doc->get_caret_column();
                                static_cast<void>(doc->insert_text(close_tag));
                                doc->set_caret(cur_line, cur_col);
                            }
                        }
                    }
                }
            }

            // Check if caret is inside parentheses '(' ... ')'
            const std::string_view prefix_before_caret = current_line.substr(0, std::min(caret_col, current_line.size()));
            std::size_t open_parens = 0;
            std::size_t close_parens = 0;
            for (char ch : prefix_before_caret)
            {
                if (ch == '(') ++open_parens;
                else if (ch == ')') ++close_parens;
            }

            const bool is_inside_parens = (open_parens > close_parens);

            if (is_inside_parens && (utf8_text == "(" || utf8_text == "," || m_signature_help.is_visible() || !current_word.empty()))
            {
                Language::Protocol::Position sig_pos{
                    .line = doc->get_caret_line(),
                    .character = doc->get_caret_column()
                };
                Language::LanguageServerManager::instance().request_signature_help(
                    uri, fname, sig_pos, current_line,
                    [this](std::optional<Language::Protocol::SignatureHelp> help) {
                        std::lock_guard<std::mutex> lock(m_lsp_mutex);
                        if (const auto* current_doc = m_controller.get_active_document())
                        {
                            const std::string_view line = current_doc->get_line(current_doc->get_caret_line());
                            const std::size_t col = current_doc->get_caret_column();
                            const std::string_view pfx = line.substr(0, std::min(col, line.size()));
                            std::size_t op = 0;
                            std::size_t cp = 0;
                            for (char ch : pfx)
                            {
                                if (ch == '(') ++op;
                                else if (ch == ')') ++cp;
                            }
                            if (op <= cp)
                            {
                                m_signature_help.hide();
                                return;
                            }
                        }
                        if (help.has_value() && !help->signatures.empty())
                        {
                            m_signature_help.show(std::move(*help), 0.0F, 0.0F);
                        }
                        else
                        {
                            m_signature_help.hide();
                        }
                        if (m_window_handle != nullptr)
                        {
                            InvalidateRect(m_window_handle, nullptr, FALSE);
                        }
                    }
                );
            }
            else
            {
                std::lock_guard<std::mutex> lock(m_lsp_mutex);
                m_signature_help.hide();
            }

            if (utf8_text == " " || utf8_text == ";" || utf8_text == ")" || utf8_text == "}")
            {
                std::lock_guard<std::mutex> lock(m_lsp_mutex);
                m_completion_popup.hide();
            }
            else if (is_trigger_char || m_completion_popup.is_visible())
            {
                Language::Protocol::Position pos{
                    .line = doc->get_caret_line(),
                    .character = doc->get_caret_column()
                };
                Language::LanguageServerManager::instance().request_completion(
                    uri, fname, pos, current_line,
                    [this](std::vector<Language::Protocol::CompletionItem> items) {
                        std::lock_guard<std::mutex> lock(m_lsp_mutex);
                        if (!items.empty())
                        {
                            m_completion_popup.show(std::move(items), 100.0F, 100.0F);

                            // Dynamically extract the latest word at caret
                            if (const auto* current_doc = m_controller.get_active_document())
                            {
                                const std::string_view line = current_doc->get_line(current_doc->get_caret_line());
                                const std::size_t col = current_doc->get_caret_column();
                                std::size_t start = std::min(col, line.size());
                                while (start > 0)
                                {
                                    const char ch = line[start - 1];
                                    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '#' || ch == '~' || ch == '/' || ch == '.' || ch == '-')
                                    {
                                        --start;
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                                const std::string_view latest_word = line.substr(start, col - start);
                                if (!latest_word.empty())
                                {
                                    m_completion_popup.set_filter(latest_word);
                                }
                            }

                            if (m_completion_popup.get_item_count() == 0)
                            {
                                m_completion_popup.hide();
                            }
                        }
                        else
                        {
                            m_completion_popup.hide();
                        }

                        if (m_window_handle != nullptr)
                        {
                            InvalidateRect(m_window_handle, nullptr, FALSE);
                        }
                    }
                );
            }
        }
    }
    return changed;
}

bool TextEditor::trigger_completion()
{
    if (auto* doc = m_controller.get_active_document(); doc != nullptr)
    {
        const std::string uri = get_active_document_uri();
        const std::string fname = get_active_document_filename();
        const Language::Protocol::Position pos{
            .line = doc->get_caret_line(),
            .character = doc->get_caret_column()
        };
        const std::string_view current_line = doc->get_line(doc->get_caret_line());

        Language::LanguageServerManager::instance().request_completion(
            uri, fname, pos, current_line,
            [this](std::vector<Language::Protocol::CompletionItem> items) {
                std::lock_guard<std::mutex> lock(m_lsp_mutex);
                if (!items.empty())
                {
                    m_completion_popup.show(std::move(items), 100.0F, 100.0F);

                    if (const auto* current_doc = m_controller.get_active_document())
                    {
                        const std::string_view line = current_doc->get_line(current_doc->get_caret_line());
                        const std::size_t col = current_doc->get_caret_column();
                        std::size_t start = std::min(col, line.size());
                        while (start > 0)
                        {
                            const char ch = line[start - 1];
                            if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '#' || ch == '~' || ch == '/' || ch == '.' || ch == '-')
                            {
                                --start;
                            }
                            else
                            {
                                break;
                            }
                        }
                        const std::string_view latest_word = line.substr(start, col - start);
                        if (!latest_word.empty())
                        {
                            m_completion_popup.set_filter(latest_word);
                        }
                    }

                    if (m_completion_popup.get_item_count() == 0)
                    {
                        m_completion_popup.hide();
                    }
                }
                else
                {
                    m_completion_popup.hide();
                }

                if (m_window_handle != nullptr)
                {
                    InvalidateRect(m_window_handle, nullptr, FALSE);
                }
            }
        );
        return true;
    }
    return false;
}

bool TextEditor::is_focused() const noexcept
{
    return m_focused;
}

UI::Editor::TextDocumentModel* TextEditor::get_focused_document() noexcept
{
    if (m_is_split && m_focused_pane == SplitPaneFocus::Right && m_split_document_index.has_value())
    {
        return m_controller.get_document(*m_split_document_index);
    }
    return m_controller.get_active_document();
}

const UI::Editor::TextDocumentModel* TextEditor::get_focused_document() const noexcept
{
    if (m_is_split && m_focused_pane == SplitPaneFocus::Right && m_split_document_index.has_value())
    {
        return m_controller.get_document(*m_split_document_index);
    }
    return m_controller.get_active_document();
}

bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        const float scale = layout.dpi_scale;
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
        const float scroll_top_y = layout.editor_bounds.y;
        const float scroll_total_h = layout.editor_bounds.height;
        const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
        const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
        const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};

        const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
        const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};

        return left_scrollbar.contains(point_x, point_y) || right_scrollbar.contains(point_x, point_y);
    }
    return m_scrollbar.is_point(layout, point_x, point_y);
}

bool TextEditor::is_minimap_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        const float scale = layout.dpi_scale;
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;
        const float scroll_top_y = layout.editor_bounds.y;
        const float scroll_total_h = layout.editor_bounds.height;
        const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
        const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
        const float left_minimap_w = (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y, left_minimap_w, scroll_total_h};

        const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
        const float right_minimap_w = (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w, scroll_top_y, right_minimap_w, scroll_total_h};

        return (!left_minimap.is_empty() && left_minimap.contains(point_x, point_y)) ||
               (!right_minimap.is_empty() && right_minimap.contains(point_x, point_y));
    }
    return m_minimap.is_point(layout, point_x, point_y);
}

bool TextEditor::check_external_file_changes()
{
    const auto reloaded_docs = m_controller.reload_externally_modified_files();
    if (reloaded_docs.empty())
    {
        return false;
    }

    for (const std::size_t idx : reloaded_docs)
    {
        if (m_controller.get_active_index() == idx)
        {
            if (const auto* doc = m_controller.get_active_document(); doc != nullptr)
            {
                const std::string uri = get_active_document_uri();
                const std::string fname = get_active_document_filename();
                std::string content;
                for (std::size_t i = 0; i < doc->get_line_count(); ++i)
                {
                    content += doc->get_line(i);
                    content += "\n";
                }
                Language::LanguageServerManager::instance().on_document_changed(
                    uri, fname, 1, content);
            }
        }
    }
    return true;
}

bool TextEditor::tick_animations() noexcept
{
    bool needs_redraw = m_focused && m_controller.get_active_document() != nullptr && m_caret_blink.tick();
    
    // Check for external file modifications
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_file_check_time).count() >= 200)
    {
        m_last_file_check_time = now;
        if (check_external_file_changes())
        {
            needs_redraw = true;
        }
    }

    // Lerp animated tab positions
    bool animating = false;
    for (auto& [doc, animated_x] : m_tab_animated_x)
    {
        if (m_tab_target_x.contains(doc))
        {
            const float target_x = m_tab_target_x[doc];
            if (std::abs(animated_x - target_x) > 0.5f)
            {
                animated_x += (target_x - animated_x) * 0.3f; // Smooth lerp
                animating = true;
            }
            else
            {
                animated_x = target_x;
            }
        }
    }
    
    if (m_selection_animation.tick())
    {
        animating = true;
    }
    if (m_split_selection_animation.tick())
    {
        animating = true;
    }
    
    if (const UI::Editor::TextDocumentModel* doc = m_controller.get_active_document())
    {
        UI::Editor::TextPosition current_caret{doc->get_caret_line(), doc->get_caret_column()};
        if (m_last_brace_caret != current_caret)
        {
            m_last_brace_caret = current_caret;
            auto [open_brace, close_brace] = find_enclosing_braces(*doc);
            m_brace_animation.set_active_braces(open_brace, close_brace);
        }
    }
    else
    {
        m_brace_animation.clear();
    }
    
    if (m_brace_animation.tick())
    {
        animating = true;
    }
    
    return needs_redraw || animating;
}

const UI::Editor::TextDocumentModel* TextEditor::get_document() const noexcept
{
    return m_controller.get_active_document();
}

void TextEditor::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    draw_tab_strip(surface, device_context, layout);
    if (!layout.editor_header_bounds.is_empty() && layout.editor_header_bounds.height > 2.0F)
    {
        draw_editor_header(surface, device_context, layout);
    }
    if (layout.editor_bounds.is_empty() || layout.editor_bounds.height <= 2.0F)
    {
        return;
    }
    draw_document(surface, device_context, layout);
    if (!m_is_split)
    {
        if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
        {
            const float line_height = 20.0F * surface.m_dpi_scale;
            const std::size_t visible_count = static_cast<std::size_t>(std::max(
                static_cast<int>(layout.editor_bounds.height / line_height), 1));
            m_minimap.render(
                surface,
                device_context,
                layout,
                *document,
                m_scrollbar.get_first_visible_line(),
                visible_count);
        }
        m_scrollbar.render(surface, device_context, layout);

        // Render Scrollbar Error / Warning Stripes (JetBrains Overview Ruler)
        if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
        {
            const std::size_t total_lines = document->get_line_count();
            if (total_lines > 0)
            {
                const float track_x = layout.scrollbar_bounds.x;
                const float track_y = layout.scrollbar_bounds.y;
                const float track_w = layout.scrollbar_bounds.width;
                const float track_h = layout.scrollbar_bounds.height;

                for (std::size_t line_idx = 0; line_idx < total_lines; ++line_idx)
                {
                    const auto diags = document->get_diagnostics_for_line(line_idx);
                    if (diags.empty()) continue;

                    bool has_err = false;
                    bool has_warn = false;
                    for (const auto& d : diags)
                    {
                        if (d.severity == Language::Protocol::DiagnosticSeverity::Error) has_err = true;
                        else if (d.severity == Language::Protocol::DiagnosticSeverity::Warning) has_warn = true;
                    }

                    const float stripe_y = track_y + (static_cast<float>(line_idx) / static_cast<float>(total_lines)) * track_h;
                    const UI::Theme::Color stripe_color = has_err
                        ? UI::Theme::Color{247, 84, 100, 255}
                        : (has_warn ? UI::Theme::Color{240, 167, 50, 255} : UI::Theme::Color{86, 182, 194, 255});

                    surface.fill_rectangle(device_context,
                        UI::Rect{track_x + 1.0F, stripe_y, track_w - 2.0F, std::max(2.5F * surface.m_dpi_scale, 2.0F)},
                        stripe_color);
                }
            }
        }
    }
    else if (m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        const float scale = surface.m_dpi_scale;
        const float line_height = 20.0F * scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

        // --- Left Pane Minimap & Scrollbar ---
        const float scroll_top_y = layout.editor_bounds.y;
        const float scroll_total_h = layout.editor_bounds.height;
        const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
        const UI::Rect left_bounds{layout.editor_bounds.x, scroll_top_y, splitter_x - layout.editor_bounds.x, scroll_total_h};
        const float left_minimap_w = (left_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect left_scrollbar{left_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect left_minimap{left_scrollbar.x - left_minimap_w, scroll_top_y, left_minimap_w, scroll_total_h};

        UI::Editor::StudioEditorLayoutResult left_layout = layout;
        left_layout.minimap_bounds = left_minimap;
        left_layout.scrollbar_bounds = left_scrollbar;

        if (const UI::Editor::TextDocumentModel* left_doc = m_controller.get_active_document())
        {
            if (!left_minimap.is_empty())
            {
                m_minimap.render(
                    surface, device_context, left_layout, *left_doc,
                    m_scrollbar.get_first_visible_line(), visible_count);
            }
            m_scrollbar.render(surface, device_context, left_layout);
        }

        // --- Right Pane Minimap & Scrollbar ---
        const UI::Rect right_bounds{splitter_x + 2.0F * scale, scroll_top_y, layout.editor_bounds.right() - (splitter_x + 2.0F * scale), scroll_total_h};
        const float right_minimap_w = (right_bounds.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
        const UI::Rect right_scrollbar{right_bounds.right() - scrollbar_w, scroll_top_y, scrollbar_w, scroll_total_h};
        const UI::Rect right_minimap{right_scrollbar.x - right_minimap_w, scroll_top_y, right_minimap_w, scroll_total_h};

        UI::Editor::StudioEditorLayoutResult right_layout = layout;
        right_layout.minimap_bounds = right_minimap;
        right_layout.scrollbar_bounds = right_scrollbar;

        if (const UI::Editor::TextDocumentModel* right_doc = m_controller.get_document(*m_split_document_index))
        {
            if (!right_minimap.is_empty())
            {
                m_split_minimap.render(
                    surface, device_context, right_layout, *right_doc,
                    m_split_scrollbar.get_first_visible_line(), visible_count);
            }
            m_split_scrollbar.render(surface, device_context, right_layout);

            // Right Overview Ruler
            const std::size_t total_lines = right_doc->get_line_count();
            if (total_lines > 0)
            {
                for (std::size_t line_idx = 0; line_idx < total_lines; ++line_idx)
                {
                    const auto diags = right_doc->get_diagnostics_for_line(line_idx);
                    if (diags.empty()) continue;
                    bool has_err = false;
                    bool has_warn = false;
                    for (const auto& d : diags)
                    {
                        if (d.severity == Language::Protocol::DiagnosticSeverity::Error) has_err = true;
                        else if (d.severity == Language::Protocol::DiagnosticSeverity::Warning) has_warn = true;
                    }
                    const float stripe_y = right_scrollbar.y + (static_cast<float>(line_idx) / static_cast<float>(total_lines)) * right_scrollbar.height;
                    const UI::Theme::Color stripe_color = has_err
                        ? UI::Theme::Color{247, 84, 100, 255}
                        : (has_warn ? UI::Theme::Color{240, 167, 50, 255} : UI::Theme::Color{86, 182, 194, 255});
                    surface.fill_rectangle(device_context,
                        UI::Rect{right_scrollbar.x + 1.0F, stripe_y, right_scrollbar.width - 2.0F, std::max(2.5F * scale, 2.0F)},
                        stripe_color);
                }
            }
        }
    }
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const std::span<const UI::Editor::EditorSessionDocument> documents =
        m_controller.get_documents();
    
    float total_width = 0.0f;
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        total_width += UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_ui_font, documents[index].text.get_file_name())),
            surface.m_dpi_scale);
        total_width += UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }
    
    m_max_tab_scroll = std::max(0.0f, total_width - layout.tab_bar_bounds.width);
    if (m_max_tab_scroll == 0.0f) {
        // Reset tab scroll if it fits entirely
        const_cast<TextEditor*>(this)->m_tab_scroll_offset = 0.0f;
    }

    SaveDC(device_context);
    IntersectClipRect(device_context, 
        static_cast<int>(layout.tab_bar_bounds.x),
        static_cast<int>(layout.tab_bar_bounds.y),
        static_cast<int>(layout.tab_bar_bounds.right()),
        static_cast<int>(layout.tab_bar_bounds.bottom()));

    m_tab_count = 0;
    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const float right_limit = layout.tab_bar_bounds.right();
    const std::optional<std::size_t> active_index = m_controller.get_active_index();
    for (std::size_t index = 0; index < documents.size(); ++index)
    {
        const UI::Editor::TextDocumentModel& document = documents[index].text;
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_ui_font, document.get_file_name())),
            surface.m_dpi_scale);
        if (tab_x > right_limit)
        {
            break;
        }
        UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == index)
        {
            bounds.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
        }
        if (m_tab_count < max_visible_tabs)
        {
            m_tab_bounds[m_tab_count] = bounds;
            ++m_tab_count;
        }
        tab_x += width +
            UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }

    auto draw_single_tab = [&](std::size_t tab_index) {
        const std::size_t index = tab_index; // Mapping is direct in the first pass
        const UI::Editor::TextDocumentModel& document = documents[index].text;
        const bool active = active_index && *active_index == index;
        const UI::Rect& bounds = m_tab_bounds[tab_index];
        const bool close_hovered = m_hovered_tab_close_index &&
                    *m_hovered_tab_close_index == tab_index;
        const bool tab_hovered = m_hovered_tab_index &&
                    *m_hovered_tab_index == tab_index;
        const bool is_dragging_this = m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == index;
        if (is_dragging_this)
        {
            const UI::Rect shadow_rect{bounds.x + 2.0F * surface.m_dpi_scale, bounds.y + 2.0F * surface.m_dpi_scale, bounds.width, bounds.height};
            surface.fill_rounded_rectangle(device_context, shadow_rect, UI::Theme::Color{0, 0, 0, 90}, 4.0F * surface.m_dpi_scale);
        }

        surface.fill_rectangle(
            device_context,
            bounds,
            is_dragging_this ? UI::Theme::Color{38, 42, 50, 255}
                   : (active ? surface.m_palette.tab_active_background
                             : (tab_hovered ? surface.m_palette.active_line_background : surface.m_palette.tab_background)));

        const UI::Theme::Color tab_edge_color = is_dragging_this
            ? UI::Theme::Color{53, 132, 228, 230}
            : surface.m_palette.border;
        const int tab_left = round_to_int(bounds.x);
        const int tab_right = round_to_int(bounds.right()) - 1;
        const int tab_top = round_to_int(bounds.y);
        const int tab_bottom = round_to_int(bounds.bottom()) - 1;

        if (active)
        {
            // Active tab top accent bar (VS Code style)
            surface.fill_rectangle(
                device_context,
                UI::Rect{bounds.x, bounds.y, bounds.width, std::max(2.0F * surface.m_dpi_scale, 2.0F)},
                surface.m_palette.accent);
            surface.draw_line(device_context, tab_left, tab_top, tab_left, tab_bottom, tab_edge_color);
            surface.draw_line(device_context, tab_right, tab_top, tab_right, tab_bottom, tab_edge_color);
        }
        else
        {
            surface.draw_line(device_context, tab_left, tab_top, tab_right, tab_top, tab_edge_color);
            surface.draw_line(device_context, tab_left, tab_top, tab_left, tab_bottom, tab_edge_color);
            surface.draw_line(device_context, tab_right, tab_top, tab_right, tab_bottom, tab_edge_color);
            surface.draw_line(device_context, tab_left, tab_bottom, tab_right, tab_bottom, tab_edge_color);
        }
                const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
                    std::filesystem::path{std::string{document.get_file_name()}});
                surface.draw_svg_icon(
                    device_context,
                    icon_asset,
                    round_to_int(bounds.x +
                        (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) *
                            surface.m_dpi_scale),
                    round_to_int(bounds.y + bounds.height * 0.5F),
                    std::max(round_to_int(14.0F * surface.m_dpi_scale), 10),
                    surface.m_palette.text_primary,
                    surface.m_palette.tab_background,
                    true);
                surface.draw_text(
                    device_context,
                    *surface.m_ui_font,
                    document.get_file_name(),
                    bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                        surface.m_dpi_scale,
                    bounds.y + bounds.height * 0.5F,
                    active ? surface.m_palette.text_primary : surface.m_palette.text_muted);
                const float close_cx = bounds.right() - UI::Editor::StudioEditorMetrics::editor_tab_close_width * 0.5F * surface.m_dpi_scale;
                const float close_cy = bounds.y + bounds.height * 0.5F;
                const int close_icon_sz = std::max(round_to_int(12.0F * surface.m_dpi_scale), 10);

                if (document.is_dirty() && !close_hovered && !tab_hovered)
                {
                    surface.draw_svg_icon(
                        device_context,
                        "dirty.svg",
                        round_to_int(close_cx),
                        round_to_int(close_cy),
                        std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
                        surface.m_palette.warning,
                        surface.m_palette.tab_background);
                }
                else if (active || tab_hovered || close_hovered)
                {
                    if (close_hovered)
                    {
                        const float pad = 2.0F * surface.m_dpi_scale;
                        const UI::Rect btn_rect{
                            close_cx - close_icon_sz * 0.5F - pad,
                            close_cy - close_icon_sz * 0.5F - pad,
                            static_cast<float>(close_icon_sz) + pad * 2.0F,
                            static_cast<float>(close_icon_sz) + pad * 2.0F
                        };
                        surface.fill_rounded_rectangle(
                            device_context, btn_rect,
                            UI::Theme::Color{255, 255, 255, 25},
                            3.0F * surface.m_dpi_scale);
                    }
                    const UI::Theme::Color close_col = close_hovered
                        ? UI::Theme::Color{255, 255, 255, 255}
                        : (active ? surface.m_palette.text_primary : surface.m_palette.text_muted);
                    surface.draw_svg_icon(
                        device_context,
                        "diagnostic-error.svg",
                        round_to_int(close_cx),
                        round_to_int(close_cy),
                        close_icon_sz,
                        close_col,
                        surface.m_palette.tab_background);
                }
    };

    for (std::size_t tab_index = 0; tab_index < m_tab_count; ++tab_index)
    {
        if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == tab_index) continue;
        draw_single_tab(tab_index);
    }
    if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() < m_tab_count)
    {
        draw_single_tab(m_tab_drag_drop.get_dragged_index());
    }



    const int tab_bar_bottom = round_to_int(layout.tab_bar_bounds.bottom()) - 1;
    const int tab_bar_left = round_to_int(layout.tab_bar_bounds.x);
    const int tab_bar_right = round_to_int(layout.tab_bar_bounds.right());
    
    if (active_index && *active_index < m_tab_count)
    {
        const UI::Rect& active_bounds = m_tab_bounds[*active_index];
        const int active_left = round_to_int(active_bounds.x);
        const int active_right = round_to_int(active_bounds.right()) - 1;
        
        surface.draw_line(device_context, tab_bar_left, tab_bar_bottom, active_left, tab_bar_bottom, surface.m_palette.border);
        surface.draw_line(device_context, active_right, tab_bar_bottom, tab_bar_right, tab_bar_bottom, surface.m_palette.border);
    }
    else
    {
        surface.draw_line(device_context, tab_bar_left, tab_bar_bottom, tab_bar_right, tab_bar_bottom, surface.m_palette.border);
    }

    RestoreDC(device_context, -1);

    if (m_max_tab_scroll > 0.0f)
    {
        const float track_width = layout.tab_bar_bounds.width;
        const float thumb_width = std::max(20.0F * surface.m_dpi_scale, track_width * (track_width / (track_width + m_max_tab_scroll)));
        const float thumb_x = layout.tab_bar_bounds.x + (m_tab_scroll_offset / m_max_tab_scroll) * (track_width - thumb_width);
        const UI::Rect thumb_bounds { thumb_x, layout.tab_bar_bounds.bottom() - 3.0F * surface.m_dpi_scale, thumb_width, 3.0F * surface.m_dpi_scale };
        const UI::Theme::Color thumb_color = m_hovered_tab_scrollbar ? surface.m_palette.accent : surface.m_palette.text_muted;
        surface.fill_rectangle(device_context, thumb_bounds, thumb_color);
    }
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (layout.editor_bounds.is_empty() || layout.editor_bounds.height <= 2.0F)
    {
        return;
    }
    if (document == nullptr)
    {
        surface.fill_rectangle(device_context, layout.editor_bounds,
                               surface.m_palette.editor_background);

        m_empty_state_open_btn.set_bounds(UI::Rect{});
        m_empty_state_clone_btn.set_bounds(UI::Rect{});

        const float dpi = surface.m_dpi_scale;
        const float logo_size = 180.0F * dpi;
        const float logo_gap = 32.0F * dpi;

        const std::string title = "Zenvra Development Studio";
        const int title_w = surface.m_large_font
            ? surface.m_large_font->getTextWidth(device_context, title)
            : (surface.m_ui_font ? surface.m_ui_font->getTextWidth(device_context, title) : static_cast<int>(240.0F * dpi));
        const float text_block_w = std::max(static_cast<float>(title_w), 260.0F * dpi);
        const float total_w = logo_size + logo_gap + text_block_w;

        const float start_x = std::max(layout.editor_bounds.x + 30.0F * dpi,
                                       layout.editor_bounds.x + (layout.editor_bounds.width - total_w) * 0.5F);
        const float start_y = layout.editor_bounds.y + layout.editor_bounds.height * 0.32F;

        // 1. Extra Large Iconic Logo on the left
        surface.draw_png_icon(
            device_context, "zenvra_logo.png",
            round_to_int(start_x + logo_size * 0.5F),
            round_to_int(start_y + logo_size * 0.5F),
            round_to_int(logo_size), surface.m_palette.editor_background);

        const float text_x = start_x + logo_size + logo_gap;

        // 2. Heading "Zenvra Development Studio"
        if (surface.m_large_font)
        {
            surface.draw_text(
                device_context, *surface.m_large_font, title, text_x, start_y + 36.0F * dpi,
                surface.m_palette.text_primary);
        }
        else if (surface.m_ui_font)
        {
            surface.draw_text(
                device_context, *surface.m_ui_font, title, text_x, start_y + 36.0F * dpi,
                surface.m_palette.text_primary);
        }

        // 3. Shortcuts list aligned directly under the heading (uniform neutral tones, no blue)
        if (surface.m_small_font || surface.m_ui_font)
        {
            auto& font = surface.m_small_font ? *surface.m_small_font : *surface.m_ui_font;
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

            for (std::size_t i = 0; i < shortcuts.size(); ++i)
            {
                const float row_y = first_row_y + static_cast<float>(i) * item_gap;
                surface.draw_text(device_context, font, shortcuts[i].key,
                                  text_x, row_y, surface.m_palette.text_primary);
                surface.draw_text(device_context, font, shortcuts[i].label,
                                  text_x + key_col_w, row_y, surface.m_palette.text_muted);
            }
        }

        return;
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const float first_center_y = layout.editor_bounds.y + line_height * 0.5F;
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale - m_text_scroll_offset;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();

    const std::size_t tab_size = document->get_status().indent_width > 0
        ? document->get_status().indent_width
        : 4;

    // Rebuild folding model from the current document lines.
    m_folding.rebuild(
        std::vector<std::string>(document->get_lines().begin(), document->get_lines().end()),
        tab_size);

    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
    if (m_reveal_caret_pending)
    {
        static_cast<void>(m_scrollbar.reveal_line(physical_line_to_visual_row(
            m_folding, document->get_caret_line(), total_lines)));
        // If caret is off-screen horizontally, we might want to reveal it too, 
        // but for now we just handle vertical.
        m_reveal_caret_pending = false;
    }
    const std::size_t first_visual_row = m_scrollbar.get_first_visible_line();
    const std::size_t first_line = visual_row_to_physical_line(
        m_folding, first_visual_row, total_lines);
    const std::size_t render_count = visible_count;
    const bool syntax_highlighting =
        UI::Editor::supports_editor_syntax_highlighting(document->get_file_name());
    const auto token_color = [&surface](UI::Editor::EditorTokenKind kind)
        -> const UI::Theme::Color& {
        switch (kind)
        {
        case UI::Editor::EditorTokenKind::Keyword: return surface.m_palette.keyword;
        case UI::Editor::EditorTokenKind::Number: return surface.m_palette.number;
        case UI::Editor::EditorTokenKind::Label: return surface.m_palette.label;
        case UI::Editor::EditorTokenKind::Type: return surface.m_palette.type;
        case UI::Editor::EditorTokenKind::Comment: return surface.m_palette.comment;
        case UI::Editor::EditorTokenKind::String: return surface.m_palette.success;
        case UI::Editor::EditorTokenKind::Plain: return surface.m_palette.text_primary;
        }
        return surface.m_palette.text_primary;
    };

    const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * surface.m_dpi_scale;
    const float gutter_line_x = layout.gutter_bounds.right() - fold_margin - 1.0F;
    surface.draw_line(
        device_context,
        round_to_int(gutter_line_x),
        round_to_int(layout.gutter_bounds.y),
        round_to_int(gutter_line_x),
        round_to_int(layout.gutter_bounds.bottom()),
        surface.m_palette.border);

    const float scale = surface.m_dpi_scale;
    const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
    const float half_w = is_split_active ? ((layout.editor_bounds.width - 2.0F * scale) * m_split_ratio) : layout.editor_bounds.width;
    const float splitter_x_coord = layout.editor_bounds.x + half_w;
    const float left_bounds_r = splitter_x_coord;
    const float scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
    const float left_minimap_w = (half_w >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
    const float left_code_limit = is_split_active ? (left_bounds_r - scrollbar_w - left_minimap_w) : (layout.editor_bounds.right() - layout.scrollbar_bounds.width - layout.minimap_bounds.width);
    const float left_right_limit = splitter_x_coord;

    // --- Indent guide rendering (VS Code style) ---
    {
        const float space_width = static_cast<float>(
            surface.get_text_width(device_context, *surface.m_editor_font, " "));
        const UI::Components::ActiveIndentScope active_scope =
            m_folding.get_active_indent_scope(document->get_caret_line(), tab_size);

        std::size_t row_guide = 0;
        for (std::size_t line_index = first_line; row_guide < render_count && line_index < total_lines; ++line_index)
        {
            if (m_folding.is_line_hidden(line_index))
            {
                continue;
            }
            const float center_y = first_center_y + static_cast<float>(row_guide) * line_height;
            ++row_guide;

            const float y_top = center_y - line_height * 0.5F;
            const float y_bottom = center_y + line_height * 0.5F;

            const std::size_t line_indent = m_folding.get_effective_indent(line_index);
            if (line_indent < tab_size)
            {
                continue;
            }

            // Cap guides to block depth so continuation lines don't create multiple bogus guides
            const std::size_t prev_indent = (line_index > 0) ? m_folding.get_effective_indent(line_index - 1) : 0;
            const std::size_t next_indent = (line_index + 1 < total_lines) ? m_folding.get_effective_indent(line_index + 1) : 0;
            const std::size_t max_allowed = std::max({prev_indent, next_indent, tab_size}) + tab_size;
            const std::size_t max_guide = std::min(line_indent, max_allowed);

            for (std::size_t col = tab_size; col <= max_guide; col += tab_size)
            {
                const float guide_x = code_x + static_cast<float>(col) * space_width;
                if (guide_x < layout.editor_bounds.x || guide_x > left_right_limit)
                {
                    continue;
                }

                const bool is_active = active_scope.valid &&
                    col == active_scope.column &&
                    line_index >= active_scope.start_line &&
                    line_index <= active_scope.end_line;

                surface.draw_line(
                    device_context,
                    round_to_int(guide_x), round_to_int(y_top),
                    round_to_int(guide_x), round_to_int(y_bottom),
                    is_active ? surface.m_palette.indent_guide_active
                              : surface.m_palette.indent_guide);
            }
        }
    }

    // Pass 1: Gutter and backgrounds
    std::size_t row_pass1 = 0;
    for (std::size_t line_index = first_line; row_pass1 < render_count && line_index < total_lines; ++line_index)
    {
        if (m_folding.is_line_hidden(line_index))
        {
            continue;
        }
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row_pass1) * line_height;
        ++row_pass1;
        const bool active_line = (line_index == document->get_caret_line()) && (!is_split_active || m_focused_pane == SplitPaneFocus::Left);
        if (active_line && !document->has_selection())
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                    left_code_limit - layout.gutter_bounds.x, line_height},
                surface.m_palette.active_line_background);
        }
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - fold_margin - 5.0F * surface.m_dpi_scale -
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_editor_font, number));
        surface.draw_text(
            device_context,
            *surface.m_editor_font,
            number,
            number_x,
            center_y,
            active_line ? surface.m_palette.text_primary : surface.m_palette.text_muted);

        const auto gutter_diags = document->get_diagnostics_for_line(line_index);
        if (!gutter_diags.empty())
        {
            bool has_error = false;
            bool has_warn = false;
            for (const auto& gd : gutter_diags)
            {
                if (gd.severity == Language::Protocol::DiagnosticSeverity::Error) has_error = true;
                else if (gd.severity == Language::Protocol::DiagnosticSeverity::Warning) has_warn = true;
            }

            const float dot_x = layout.gutter_bounds.x + 4.0F * surface.m_dpi_scale;
            const float dot_r = 3.0F * surface.m_dpi_scale;
            const UI::Theme::Color dot_color = has_error
                ? UI::Theme::Color{247, 84, 100, 255}
                : (has_warn ? UI::Theme::Color{240, 167, 50, 255} : UI::Theme::Color{86, 182, 194, 255});
            surface.fill_rounded_rectangle(device_context,
                UI::Rect{dot_x, center_y - dot_r, dot_r * 2.0F, dot_r * 2.0F},
                dot_color, dot_r);
        }

        if (has_gutter_marker(line))
        {
            const int marker_x = round_to_int(
                layout.gutter_bounds.right() - 13.0F * surface.m_dpi_scale);
            const int marker_y = round_to_int(center_y);
            const int half = std::max(round_to_int(3.0F * surface.m_dpi_scale), 2);
            POINT points[]{
                POINT{marker_x, marker_y - half},
                POINT{marker_x + half, marker_y},
                POINT{marker_x, marker_y + half},
                POINT{marker_x - half, marker_y},
            };
            HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(surface.m_palette.text_muted));
            HGDIOBJ previous_pen = SelectObject(device_context, pen);
            HGDIOBJ previous_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
            Polygon(device_context, points, 4);
            SelectObject(device_context, previous_brush);
            SelectObject(device_context, previous_pen);
            DeleteObject(pen);
        }

        // --- Fold icon rendering (clean VS Code style) ---
        const UI::Components::FoldMarker fold_marker = m_folding.get_marker(line_index);
        const float fold_center_x = layout.gutter_bounds.right() - fold_margin * 0.5F;
        const int fold_cx = round_to_int(fold_center_x);
        const int fold_cy = round_to_int(center_y);

        if (fold_marker == UI::Components::FoldMarker::Expanded ||
            fold_marker == UI::Components::FoldMarker::Collapsed)
        {
            const bool fold_hovered = m_hovered_fold_line && *m_hovered_fold_line == line_index;

            // Draw a small rounded box with +/- sign
            const int box_half = std::max(round_to_int(4.5F * surface.m_dpi_scale), 4);
            RECT box_rect{
                fold_cx - box_half, fold_cy - box_half,
                fold_cx + box_half, fold_cy + box_half};

            HBRUSH bg_brush = CreateSolidBrush(to_color_ref(active_line ? surface.m_palette.active_line_background : surface.m_palette.editor_background));
            FillRect(device_context, &box_rect, bg_brush);
            DeleteObject(bg_brush);

            HPEN box_pen = CreatePen(PS_SOLID, 1, to_color_ref(fold_hovered ? surface.m_palette.accent : surface.m_palette.border));
            HGDIOBJ old_pen = SelectObject(device_context, box_pen);
            HGDIOBJ old_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
            Rectangle(device_context, box_rect.left, box_rect.top, box_rect.right, box_rect.bottom);
            SelectObject(device_context, old_brush);
            SelectObject(device_context, old_pen);
            DeleteObject(box_pen);

            // Horizontal line of +/- (always present)
            const int sign_inset = std::max(round_to_int(2.0F * surface.m_dpi_scale), 2);
            surface.draw_line(device_context,
                fold_cx - box_half + sign_inset, fold_cy,
                fold_cx + box_half - sign_inset, fold_cy,
                fold_hovered ? surface.m_palette.accent : surface.m_palette.text_muted);

            if (fold_marker == UI::Components::FoldMarker::Collapsed)
            {
                // Vertical line of + (only when collapsed)
                surface.draw_line(device_context,
                    fold_cx, fold_cy - box_half + sign_inset,
                    fold_cx, fold_cy + box_half - sign_inset,
                    fold_hovered ? surface.m_palette.accent : surface.m_palette.text_muted);
            }
        }
    }

    // Pass 2: Text rendering with clipping
    SaveDC(device_context);
    const float hscroll_height = (m_max_text_scroll > 0.0f) ? 14.0F * surface.m_dpi_scale : 0.0f;
    IntersectClipRect(device_context,
        static_cast<int>(layout.editor_bounds.x),
        static_cast<int>(layout.editor_bounds.y),
        static_cast<int>(left_code_limit),
        static_cast<int>(layout.editor_bounds.bottom() - hscroll_height));

    std::vector<UI::Rect> selection_targets;
    if (!is_split_active || m_focused_pane == SplitPaneFocus::Left)
    {
        for (const auto& cursor : document->get_all_cursors())
        {
            if (cursor.has_selection())
            {
                const UI::Editor::TextSelection selection = cursor.get_selection();
                const std::size_t start_line = std::max(selection.start.line, first_line);
                const std::size_t end_line = std::min(selection.end.line, first_line + render_count);
                
                for (std::size_t line_index = start_line; line_index <= end_line; ++line_index)
                {
                    const std::string_view line = document->get_line(line_index);
                    const std::size_t selection_start = line_index == selection.start.line ? selection.start.column : 0;
                    const std::size_t selection_end = line_index == selection.end.line ? selection.end.column : line.size();
                    
                    const float selection_x = static_cast<float>(surface.get_text_width(
                        device_context, *surface.m_editor_font,
                        line.substr(0, selection_start)));
                    float selection_width = static_cast<float>(surface.get_text_width(
                        device_context, *surface.m_editor_font,
                        line.substr(selection_start, selection_end - selection_start)));
                        
                    if (line_index < selection.end.line)
                    {
                        selection_width += 6.0F * surface.m_dpi_scale;
                    }
                    
                    selection_targets.push_back(UI::Rect{
                        selection_x,
                        static_cast<float>(physical_line_to_visual_row(
                            m_folding, line_index, total_lines)) * line_height,
                        selection_width,
                        line_height
                    });
                }
            }
        }
    }
    if (selection_targets.empty())
    {
        m_selection_animation.clear();
    }
    if (!selection_targets.empty())
    {
        m_selection_animation.set_targets(selection_targets);
    }
    
    if (m_selection_animation.has_rects())
    {
        for (const UI::Rect& anim_rect : m_selection_animation.get_animated_rects())
        {
            if (anim_rect.width <= 0.0F) continue;
            
            const float screen_y = layout.editor_bounds.y + anim_rect.y - static_cast<float>(first_visual_row) * line_height;
            const float screen_x = code_x + anim_rect.x;
            
            if (screen_y + anim_rect.height >= layout.editor_bounds.y &&
                screen_y <= layout.editor_bounds.bottom())
            {
                const int snap_y = round_to_int(screen_y);
                const int snap_bottom = round_to_int(screen_y + anim_rect.height);
                const int snap_x = round_to_int(screen_x);
                const int snap_right = round_to_int(screen_x + anim_rect.width);
                
                surface.fill_rounded_rectangle(
                    device_context,
                    UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y), 
                             static_cast<float>(snap_right - snap_x), static_cast<float>(snap_bottom - snap_y)},
                    surface.m_palette.selection_background,
                    4.0F * surface.m_dpi_scale
                );
            }
        }
    }
    
    float max_line_width = 0.0f;

    std::size_t row_pass2 = 0;
    for (std::size_t line_index = first_line; row_pass2 < render_count && line_index < total_lines; ++line_index)
    {
        if (m_folding.is_line_hidden(line_index))
        {
            continue;
        }
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row_pass2) * line_height;
        ++row_pass2;
        
        const float current_line_width = static_cast<float>(surface.get_text_width(
            device_context, *surface.m_editor_font, line));
        if (current_line_width > max_line_width) max_line_width = current_line_width;


        const auto line_diags = document->get_diagnostics_for_line(line_index);
        auto get_effective_token_color = [&](UI::Editor::EditorTokenKind kind, std::size_t tok_start, std::size_t tok_len) -> UI::Theme::Color
        {
            const UI::Theme::Color base = token_color(kind);
            bool is_unnecessary = false;
            for (const auto& d : line_diags)
            {
                if (d.is_unnecessary())
                {
                    const std::size_t d_start = (d.range.start.line == line_index) ? d.range.start.character : 0;
                    const std::size_t d_end = (d.range.end.line == line_index) ? (d.range.end.character == 0 ? line.size() : d.range.end.character) : line.size();
                    if (tok_start < d_end && (tok_start + tok_len) > d_start)
                    {
                        is_unnecessary = true;
                        break;
                    }
                }
            }
            if (is_unnecessary)
            {
                // Dimmed / faded gray like VS Code unused code
                return UI::Theme::Color{
                    static_cast<uint8_t>((base.red * 35 + 115 * 65) / 100),
                    static_cast<uint8_t>((base.green * 35 + 120 * 65) / 100),
                    static_cast<uint8_t>((base.blue * 35 + 130 * 65) / 100),
                    170
                };
            }
            return base;
        };

        if (syntax_highlighting)
        {
            float token_x = code_x;
            std::size_t rendered_bytes = 0;
            std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
            const std::size_t token_count = UI::Editor::tokenize_editor_line(line, tokens, document->get_file_name());
            for (std::size_t token_index = 0; token_index < token_count; ++token_index)
            {
                const UI::Editor::EditorToken& token = tokens[token_index];
                
                bool has_animated_brace = false;
                std::size_t brace_offset = 0;
                
                if (m_brace_animation.has_active_braces() && m_brace_animation.get_pulse_scale() > 1.01F)
                {
                    if (auto open_pos = m_brace_animation.get_open_brace(); open_pos && open_pos->line == line_index)
                    {
                        if (open_pos->column >= rendered_bytes && open_pos->column < rendered_bytes + token.text.size())
                        {
                            has_animated_brace = true;
                            brace_offset = open_pos->column - rendered_bytes;
                        }
                    }
                    if (auto close_pos = m_brace_animation.get_close_brace(); close_pos && close_pos->line == line_index)
                    {
                        if (close_pos->column >= rendered_bytes && close_pos->column < rendered_bytes + token.text.size())
                        {
                            has_animated_brace = true;
                            brace_offset = close_pos->column - rendered_bytes;
                        }
                    }
                }
                
                if (has_animated_brace)
                {
                    if (brace_offset > 0)
                    {
                        std::string_view pre = token.text.substr(0, brace_offset);
                        surface.draw_text(device_context, *surface.m_editor_font, pre, token_x, center_y, get_effective_token_color(token.kind, rendered_bytes, brace_offset));
                        token_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, pre));
                    }
                    
                    std::string_view brace_char = token.text.substr(brace_offset, 1);
                    float pulse = m_brace_animation.get_pulse_scale();
                    float brace_w = static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, brace_char));
                    
                    // The 'selection touch' background requested by the user
                    float extra_w = (brace_w * pulse - brace_w) * 0.5F;
                    float extra_h = (line_height * pulse - line_height) * 0.5F;
                    float screen_y = center_y - line_height * 0.5F;
                    
                    UI::Theme::Color pulse_color = surface.m_palette.selection_background;
                    pulse_color.red = std::min(pulse_color.red + 30, 255);
                    pulse_color.green = std::min(pulse_color.green + 30, 255);
                    pulse_color.blue = std::min(pulse_color.blue + 30, 255);
                    
                    surface.fill_rounded_rectangle(
                        device_context,
                        UI::Rect{token_x - extra_w - 2.0F, screen_y - extra_h, brace_w + extra_w * 2.0F + 4.0F, line_height + extra_h * 2.0F},
                        pulse_color,
                        3.0F * surface.m_dpi_scale * pulse
                    );
                    
                    // Draw the scaled character itself over the background
                    surface.draw_scaled_text(device_context, *surface.m_editor_font, brace_char, token_x, center_y, pulse, surface.m_palette.accent);
                    token_x += brace_w;
                    
                    if (brace_offset + 1 < token.text.size())
                    {
                        std::string_view post = token.text.substr(brace_offset + 1);
                        surface.draw_text(device_context, *surface.m_editor_font, post, token_x, center_y, get_effective_token_color(token.kind, rendered_bytes + brace_offset + 1, post.size()));
                        token_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, post));
                    }
                }
                else
                {
                    surface.draw_text(
                        device_context,
                        *surface.m_editor_font,
                        token.text,
                        token_x,
                        center_y,
                        get_effective_token_color(token.kind, rendered_bytes, token.text.size()));
                    token_x += static_cast<float>(surface.get_text_width(
                        device_context, *surface.m_editor_font, token.text));
                }
                rendered_bytes += token.text.size();
            }
            if (rendered_bytes < line.size())
            {
                surface.draw_text(device_context, *surface.m_editor_font,
                    line.substr(rendered_bytes), token_x, center_y,
                    surface.m_palette.text_primary);
            }
        }
        else
        {
            bool has_unnecessary = false;
            for (const auto& d : line_diags)
            {
                if (d.is_unnecessary()) { has_unnecessary = true; break; }
            }
            const UI::Theme::Color text_col = has_unnecessary
                ? UI::Theme::Color{115, 120, 130, 170}
                : surface.m_palette.text_primary;
            surface.draw_text(
                device_context,
                *surface.m_editor_font,
                line,
                code_x,
                center_y,
                text_col);
        }

        // Render diagnostics squiggles under erroneous tokens
        for (const auto& diag : line_diags)
        {
            std::size_t start_col = diag.range.start.line == line_index ? diag.range.start.character : 0;
            std::size_t end_col = diag.range.end.line == line_index ? diag.range.end.character : line.size();
            if (end_col > line.size()) end_col = line.size();
            if (start_col >= end_col) end_col = std::min(start_col + 1, line.size());

            float diag_start_x = code_x;
            if (start_col > 0 && start_col <= line.size())
            {
                diag_start_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, line.substr(0, start_col)));
            }
            float diag_width = 8.0F;
            if (end_col > start_col && start_col < line.size())
            {
                diag_width = static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, line.substr(start_col, end_col - start_col)));
            }

            UI::Theme::Color squiggle_color = diag.severity == Language::Protocol::DiagnosticSeverity::Error
                ? UI::Theme::Color{247, 84, 100, 255}
                : (diag.severity == Language::Protocol::DiagnosticSeverity::Warning
                    ? UI::Theme::Color{240, 167, 50, 255}
                    : UI::Theme::Color{86, 182, 194, 255});

            // Draw crisp sinusoidal wavy squiggle
            float wave_x = diag_start_x;
            const float wave_end_x = diag_start_x + std::max(diag_width, 6.0F);
            const float wave_y = center_y + line_height * 0.42F;
            const float wave_step = 3.0F * surface.m_dpi_scale;
            const float wave_amp = 1.5F * surface.m_dpi_scale;
            bool wave_up = true;
            while (wave_x < wave_end_x)
            {
                const float next_x = std::min(wave_x + wave_step, wave_end_x);
                const float y1 = wave_up ? (wave_y - wave_amp) : (wave_y + wave_amp);
                const float y2 = wave_up ? (wave_y + wave_amp) : (wave_y - wave_amp);
                surface.draw_line(device_context,
                    round_to_int(wave_x), round_to_int(y1),
                    round_to_int(next_x), round_to_int(y2),
                    squiggle_color);
                wave_x = next_x;
                wave_up = !wave_up;
            }
        }

        // Render Modern Flat Inline Diagnostic Lens (flex-centered)
        if (!line_diags.empty())
        {
            const auto* top_diag = &line_diags[0];
            for (const auto& d : line_diags)
            {
                if (d.severity < top_diag->severity)
                {
                    top_diag = &d;
                }
            }

            const float dpi = surface.m_dpi_scale;
            UI::Theme::Color badge_bg{44, 20, 26, 210};
            UI::Theme::Color badge_border{247, 84, 100, 80};
            UI::Theme::Color badge_fg{255, 120, 135, 255};
            UI::Theme::Color icon_color{247, 84, 100, 255};
            std::string_view icon_asset = "diagnostic-error.svg";

            if (top_diag->severity == Language::Protocol::DiagnosticSeverity::Warning)
            {
                badge_bg = UI::Theme::Color{44, 32, 14, 210};
                badge_border = UI::Theme::Color{240, 167, 50, 80};
                badge_fg = UI::Theme::Color{250, 188, 80, 255};
                icon_color = UI::Theme::Color{240, 167, 50, 255};
                icon_asset = "diagnostic-warning.svg";
            }
            else if (top_diag->severity >= Language::Protocol::DiagnosticSeverity::Information)
            {
                badge_bg = UI::Theme::Color{18, 34, 46, 210};
                badge_border = UI::Theme::Color{86, 182, 194, 80};
                badge_fg = UI::Theme::Color{105, 210, 225, 255};
                icon_color = UI::Theme::Color{86, 182, 194, 255};
                icon_asset = "diagnostic-info.svg";
            }

            std::string msg = top_diag->message;
            for (char& ch : msg) { if (ch == '\r' || ch == '\n') ch = ' '; }

            const bool is_collapsed = (m_folding.get_marker(line_index) == UI::Components::FoldMarker::Collapsed);
            const float lens_start_x = code_x + current_line_width + (is_collapsed ? 42.0F * dpi : 20.0F * dpi);
            const float avail_w = std::max(left_code_limit - lens_start_x - 16.0F * dpi, 80.0F * dpi);

            const float pad_x = 8.0F * dpi;
            const float icon_size = 12.0F * dpi;
            const float gap = 6.0F * dpi;

            int msg_w = surface.get_text_width(device_context, *surface.m_ui_font, msg);
            const float max_msg_w = std::max(avail_w - (pad_x * 2.0F + icon_size + gap), 40.0F * dpi);
            if (static_cast<float>(msg_w) > max_msg_w && msg.size() > 8)
            {
                while (msg.size() > 4 && static_cast<float>(surface.get_text_width(device_context, *surface.m_ui_font, msg + "...")) > max_msg_w)
                {
                    msg.pop_back();
                }
                msg += "...";
                msg_w = surface.get_text_width(device_context, *surface.m_ui_font, msg);
            }

            const float badge_w = pad_x + icon_size + gap + static_cast<float>(msg_w) + pad_x;
            const float badge_h = 18.0F * dpi;
            const UI::Rect badge_rect{lens_start_x, center_y - badge_h * 0.5F, badge_w, badge_h};

            // 1. Flat Pill Background + Subtle Border
            surface.fill_rounded_rectangle(device_context, badge_rect, badge_bg, 4.0F * dpi);
            surface.draw_rectangle(device_context, badge_rect, badge_border);

            // 2. Vector SVG Icon (flex-centered)
            const int icon_cx = round_to_int(lens_start_x + pad_x + icon_size * 0.5F);
            const int icon_cy = round_to_int(center_y);
            surface.draw_svg_icon(
                device_context, icon_asset,
                icon_cx, icon_cy,
                std::max(round_to_int(icon_size), 10),
                icon_color, badge_bg);

            // 3. Message Text (flex-centered vertically)
            const float text_x = lens_start_x + pad_x + icon_size + gap;
            surface.draw_text(device_context, *surface.m_ui_font, msg, text_x, center_y, badge_fg);
        }

        // Collapsed code placeholder badge (...)
        if (m_folding.get_marker(line_index) == UI::Components::FoldMarker::Collapsed)
        {
            const float badge_x = code_x + current_line_width + 8.0F * surface.m_dpi_scale;
            const float badge_w = 26.0F * surface.m_dpi_scale;
            const float badge_h = 16.0F * surface.m_dpi_scale;
            const UI::Rect badge_rect{badge_x, center_y - badge_h * 0.5F, badge_w, badge_h};
            surface.fill_rounded_rectangle(device_context, badge_rect, UI::Theme::Color{45, 50, 65, 230}, 3.0F * surface.m_dpi_scale);
            surface.draw_rectangle(device_context, badge_rect, UI::Theme::Color{75, 84, 110, 255});
            surface.draw_text(device_context, *surface.m_small_font, "...", badge_x + 6.0F * surface.m_dpi_scale, center_y, UI::Theme::Color{210, 215, 230, 255});
        }
        if (m_focused && m_caret_blink.is_visible() && (!is_split_active || m_focused_pane == SplitPaneFocus::Left))
        {
            for (const auto& cur : document->get_all_cursors())
            {
                if (cur.line == line_index)
                {
                    const std::string_view prefix = line.substr(0, std::min(cur.column, line.size()));
                    const int caret_x = round_to_int(
                        code_x + static_cast<float>(surface.get_text_width(
                            device_context, *surface.m_editor_font, prefix)));
                    surface.draw_line(
                        device_context,
                        caret_x,
                        round_to_int(center_y - 8.0F * surface.m_dpi_scale),
                        caret_x,
                        round_to_int(center_y + 8.0F * surface.m_dpi_scale),
                        surface.m_palette.text_primary);
                }
            }
        }
    }

    RestoreDC(device_context, -1);

    // Update max scroll
    const float content_width = max_line_width + 28.0F * surface.m_dpi_scale; // with padding
    const float new_max_scroll = std::max(0.0f, content_width - layout.editor_bounds.width);
    if (new_max_scroll > m_max_text_scroll) 
    {
        m_max_text_scroll = new_max_scroll;
    }
    else if (new_max_scroll < m_max_text_scroll * 0.8f) // decay slowly if max shrunk (e.g. file changed)
    {
        m_max_text_scroll = new_max_scroll;
    }
    
    // clamp
    if (m_max_text_scroll == 0.0f) const_cast<TextEditor*>(this)->m_text_scroll_offset = 0.0f;
    else if (m_text_scroll_offset > m_max_text_scroll) const_cast<TextEditor*>(this)->m_text_scroll_offset = m_max_text_scroll;

    // Draw horizontal scrollbar thumb if needed
    if (m_max_text_scroll > 0.0f)
    {
        const float track_width = layout.editor_bounds.width;
        const float track_height = 14.0F * surface.m_dpi_scale;
        const float track_y = layout.editor_bounds.bottom() - track_height;
        const float thumb_width = std::max(20.0F * surface.m_dpi_scale, track_width * (track_width / content_width));
        const float thumb_x = layout.editor_bounds.x + (m_text_scroll_offset / m_max_text_scroll) * (track_width - thumb_width);
        const float thumb_height = 6.0F * surface.m_dpi_scale;
        const UI::Rect thumb_bounds { 
            thumb_x, 
            track_y + (track_height - thumb_height) * 0.5F, 
            thumb_width, 
            thumb_height 
        };
        surface.fill_rectangle(device_context, thumb_bounds, surface.m_palette.text_muted);
    }

    if (m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size())
    {
        auto* split_doc_ptr = m_controller.get_document(*m_split_document_index);
        if (split_doc_ptr != nullptr)
        {
            auto& split_doc = *split_doc_ptr;
            const float scale = surface.m_dpi_scale;
            const float splitter_x = left_right_limit;
            const float splitter_w = 2.0F * scale;

            // 1. SOLID Splitter vertical bar
            const UI::Theme::Color splitter_col = (m_is_resizing_split || m_hovered_split_resize)
                ? UI::Theme::Color{53, 132, 228, 255}
                : surface.m_palette.border;
            surface.fill_rectangle(
                device_context,
                UI::Rect{splitter_x, layout.editor_bounds.y, splitter_w, layout.editor_bounds.height},
                splitter_col);

            // 2. Right Pane Bounds
            const float right_x = splitter_x + splitter_w;
            const float right_w = std::max(layout.editor_bounds.right() - right_x, 0.0F);
            const UI::Rect right_pane{
                right_x,
                layout.editor_bounds.y,
                right_w,
                layout.editor_bounds.height
            };
            const float right_gutter_w = layout.gutter_bounds.width;
            const UI::Rect right_gutter{
                right_pane.x,
                right_pane.y,
                right_gutter_w,
                right_pane.height
            };
            const float right_scrollbar_w = FIXED_SCROLLBAR_WIDTH * scale;
            const float right_minimap_w = (right_pane.width >= MIN_PANE_WIDTH_FOR_MINIMAP * scale) ? (FIXED_MINIMAP_WIDTH * scale) : 0.0F;
            const float right_code_limit = std::max(right_pane.right() - right_scrollbar_w - right_minimap_w, right_gutter.right());
            const UI::Rect right_code{
                right_gutter.right(),
                right_pane.y,
                std::max(right_code_limit - right_gutter.right(), 0.0F),
                right_pane.height
            };

            // 3. SOLID Editor Background Fill and Gutter Background Fill
            surface.fill_rectangle(device_context, right_pane, surface.m_palette.editor_background);
            surface.fill_rectangle(device_context, right_gutter, surface.m_palette.editor_background);

            // 4. Gutter separator line for right pane
            const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * scale;
            const float right_gutter_line_x = right_gutter.right() - fold_margin - 1.0F;
            surface.draw_line(
                device_context,
                round_to_int(right_gutter_line_x), round_to_int(right_pane.y),
                round_to_int(right_gutter_line_x), round_to_int(right_pane.bottom()),
                surface.m_palette.border);

            const std::size_t split_total_lines = split_doc.get_line_count();
            m_split_folding.rebuild(split_doc.get_lines());
            m_split_scrollbar.synchronize(count_visible_lines(m_split_folding, split_total_lines), visible_count);
            const std::size_t split_first_line = m_split_scrollbar.get_first_visible_line();

            // Right Pass 1: Gutter and active line background
            std::size_t r_pass1 = 0;
            for (std::size_t line_index = split_first_line; r_pass1 < visible_count && line_index < split_total_lines; ++line_index)
            {
                if (m_split_folding.is_line_hidden(line_index)) continue;
                const float cy = first_center_y + static_cast<float>(r_pass1) * line_height;
                ++r_pass1;

                const bool is_active_line = (line_index == split_doc.get_caret_line()) && (m_focused_pane == SplitPaneFocus::Right);
                if (is_active_line && !split_doc.has_selection())
                {
                    surface.fill_rectangle(
                        device_context,
                        UI::Rect{right_gutter.x, cy - line_height * 0.5F, right_code.right() - right_gutter.x, line_height},
                        surface.m_palette.active_line_background);
                }

                const std::string num_str = std::to_string(line_index + 1);
                const float nx = right_gutter.right() - fold_margin - 5.0F * scale -
                    static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, num_str));
                surface.draw_text(device_context, *surface.m_editor_font, num_str, nx, cy,
                    is_active_line ? surface.m_palette.text_primary : surface.m_palette.text_muted);

                const auto gutter_diags = split_doc.get_diagnostics_for_line(line_index);
                if (!gutter_diags.empty())
                {
                    bool has_error = false;
                    bool has_warn = false;
                    for (const auto& gd : gutter_diags)
                    {
                        if (gd.severity == Language::Protocol::DiagnosticSeverity::Error) has_error = true;
                        else if (gd.severity == Language::Protocol::DiagnosticSeverity::Warning) has_warn = true;
                    }
                    const float dot_x = right_gutter.x + 4.0F * scale;
                    const float dot_r = 3.0F * scale;
                    const UI::Theme::Color dot_color = has_error
                        ? UI::Theme::Color{247, 84, 100, 255}
                        : (has_warn ? UI::Theme::Color{240, 167, 50, 255} : UI::Theme::Color{86, 182, 194, 255});
                    surface.fill_rounded_rectangle(device_context,
                        UI::Rect{dot_x, cy - dot_r, dot_r * 2.0F, dot_r * 2.0F},
                        dot_color, dot_r);
                }

                // Right Pane Fold Markers
                const UI::Components::FoldMarker r_fold = m_split_folding.get_marker(line_index);
                const float r_fold_center_x = right_gutter.right() - fold_margin * 0.5F;
                const int r_cx = round_to_int(r_fold_center_x);
                const int r_cy = round_to_int(cy);

                if (r_fold == UI::Components::FoldMarker::Expanded ||
                    r_fold == UI::Components::FoldMarker::Collapsed)
                {
                    const int box_half = std::max(round_to_int(4.5F * scale), 4);
                    RECT box_rect{r_cx - box_half, r_cy - box_half, r_cx + box_half, r_cy + box_half};
                    HBRUSH bg_brush = CreateSolidBrush(to_color_ref(is_active_line ? surface.m_palette.active_line_background : surface.m_palette.editor_background));
                    FillRect(device_context, &box_rect, bg_brush);
                    DeleteObject(bg_brush);

                    HPEN box_pen = CreatePen(PS_SOLID, 1, to_color_ref(surface.m_palette.border));
                    HGDIOBJ old_pen = SelectObject(device_context, box_pen);
                    HGDIOBJ old_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
                    Rectangle(device_context, box_rect.left, box_rect.top, box_rect.right, box_rect.bottom);
                    SelectObject(device_context, old_brush);
                    SelectObject(device_context, old_pen);
                    DeleteObject(box_pen);

                    const int sign_inset = std::max(round_to_int(2.0F * scale), 2);
                    surface.draw_line(device_context,
                        r_cx - box_half + sign_inset, r_cy,
                        r_cx + box_half - sign_inset, r_cy,
                        surface.m_palette.text_muted);

                    if (r_fold == UI::Components::FoldMarker::Collapsed)
                    {
                        surface.draw_line(device_context,
                            r_cx, r_cy - box_half + sign_inset,
                            r_cx, r_cy + box_half - sign_inset,
                            surface.m_palette.text_muted);
                    }
                }
                else if (r_fold == UI::Components::FoldMarker::Continuation)
                {
                    // Vertical scope guide line
                    surface.draw_line(device_context,
                        r_cx, round_to_int(cy - line_height * 0.5F),
                        r_cx, round_to_int(cy + line_height * 0.5F),
                        surface.m_palette.border);
                }
                else if (r_fold == UI::Components::FoldMarker::End)
                {
                    // Corner guide (╰)
                    surface.draw_line(device_context,
                        r_cx, round_to_int(cy - line_height * 0.5F),
                        r_cx, r_cy,
                        surface.m_palette.border);
                    surface.draw_line(device_context,
                        r_cx, r_cy,
                        r_cx + round_to_int(fold_margin * 0.35F), r_cy,
                        surface.m_palette.border);
                }

                if (r_fold == UI::Components::FoldMarker::Expanded)
                {
                    const int box_half = std::max(round_to_int(5.0F * scale), 4);
                    surface.draw_line(device_context,
                        r_cx, r_cy + box_half,
                        r_cx, round_to_int(cy + line_height * 0.5F),
                        surface.m_palette.border);
                }
            }

            // Right Pass 2: Text rendering with strict clipping to right_code
            SaveDC(device_context);
            IntersectClipRect(device_context,
                static_cast<int>(right_code.x),
                static_cast<int>(right_pane.y),
                static_cast<int>(right_code.right()),
                static_cast<int>(right_pane.bottom()));

            const float right_code_x = right_code.x + 14.0F * scale;

            // Right Selection Highlight with smooth animation
            std::vector<UI::Rect> split_selection_targets;
            if (m_focused_pane == SplitPaneFocus::Right)
            {
                for (const auto& cursor : split_doc.get_all_cursors())
                {
                    if (cursor.has_selection())
                    {
                        const UI::Editor::TextSelection sel = cursor.get_selection();
                        const std::size_t s_line = std::max(sel.start.line, split_first_line);
                        const std::size_t e_line = std::min(sel.end.line, split_first_line + visible_count);
                        for (std::size_t li = s_line; li <= e_line && li < split_total_lines; ++li)
                        {
                            const std::string_view line_str = split_doc.get_line(li);
                            const std::size_t sel_start = (li == sel.start.line) ? sel.start.column : 0;
                            const std::size_t sel_end = (li == sel.end.line) ? sel.end.column : line_str.size();
                            const float sel_x = static_cast<float>(surface.get_text_width(
                                device_context, *surface.m_editor_font, line_str.substr(0, sel_start)));
                            float sel_w = static_cast<float>(surface.get_text_width(
                                device_context, *surface.m_editor_font, line_str.substr(sel_start, sel_end - sel_start)));
                            if (li < sel.end.line) sel_w += 6.0F * scale;

                            split_selection_targets.push_back(UI::Rect{
                                sel_x,
                                static_cast<float>(physical_line_to_visual_row(
                                    m_split_folding, li, split_total_lines)) * line_height,
                                sel_w,
                                line_height
                            });
                        }
                    }
                }
            }

            if (split_selection_targets.empty())
            {
                m_split_selection_animation.clear();
            }
            else
            {
                m_split_selection_animation.set_targets(split_selection_targets);
            }

            const std::size_t split_first_visual_row = m_split_scrollbar.get_first_visible_line();
            if (m_split_selection_animation.has_rects())
            {
                for (const UI::Rect& anim_rect : m_split_selection_animation.get_animated_rects())
                {
                    if (anim_rect.width <= 0.0F) continue;

                    const float screen_y = right_pane.y + anim_rect.y - static_cast<float>(split_first_visual_row) * line_height;
                    const float screen_x = right_code_x + anim_rect.x;

                    if (screen_y + anim_rect.height >= right_pane.y &&
                        screen_y <= right_pane.bottom())
                    {
                        const int snap_y = round_to_int(screen_y);
                        const int snap_bottom = round_to_int(screen_y + anim_rect.height);
                        const int snap_x = round_to_int(screen_x);
                        const int snap_right = round_to_int(screen_x + anim_rect.width);

                        surface.fill_rounded_rectangle(
                            device_context,
                            UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y),
                                     static_cast<float>(snap_right - snap_x), static_cast<float>(snap_bottom - snap_y)},
                            surface.m_palette.selection_background, 4.0F * scale);
                    }
                }
            }

            const bool right_syntax = UI::Editor::supports_editor_syntax_highlighting(split_doc.get_file_name());
            std::size_t r_pass2 = 0;
            for (std::size_t line_index = split_first_line; r_pass2 < visible_count && line_index < split_total_lines; ++line_index)
            {
                if (m_split_folding.is_line_hidden(line_index)) continue;
                const float cy = first_center_y + static_cast<float>(r_pass2) * line_height;
                ++r_pass2;

                const std::string_view lstr = split_doc.get_line(line_index);
                float tok_x = right_code_x;

                const auto r_diags = split_doc.get_diagnostics_for_line(line_index);
                auto get_effective_r_token_color = [&](UI::Editor::EditorTokenKind kind, std::size_t tok_start, std::size_t tok_len) -> UI::Theme::Color
                {
                    const UI::Theme::Color base = token_color(kind);
                    bool is_unnecessary = false;
                    for (const auto& d : r_diags)
                    {
                        if (d.is_unnecessary())
                        {
                            const std::size_t d_start = (d.range.start.line == line_index) ? d.range.start.character : 0;
                            const std::size_t d_end = (d.range.end.line == line_index) ? (d.range.end.character == 0 ? lstr.size() : d.range.end.character) : lstr.size();
                            if (tok_start < d_end && (tok_start + tok_len) > d_start)
                            {
                                is_unnecessary = true;
                                break;
                            }
                        }
                    }
                    if (is_unnecessary)
                    {
                        return UI::Theme::Color{
                            static_cast<uint8_t>((base.red * 35 + 115 * 65) / 100),
                            static_cast<uint8_t>((base.green * 35 + 120 * 65) / 100),
                            static_cast<uint8_t>((base.blue * 35 + 130 * 65) / 100),
                            170
                        };
                    }
                    return base;
                };

                if (right_syntax)
                {
                    std::size_t rbytes = 0;
                    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> rtokens{};
                    const std::size_t rcount = UI::Editor::tokenize_editor_line(lstr, rtokens, split_doc.get_file_name());
                    for (std::size_t ti = 0; ti < rcount; ++ti)
                    {
                        const auto& t = rtokens[ti];
                        surface.draw_text(device_context, *surface.m_editor_font, t.text, tok_x, cy, get_effective_r_token_color(t.kind, rbytes, t.text.size()));
                        tok_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, t.text));
                        rbytes += t.text.size();
                    }
                    if (rbytes < lstr.size())
                    {
                        surface.draw_text(device_context, *surface.m_editor_font, lstr.substr(rbytes), tok_x, cy, surface.m_palette.text_primary);
                    }
                }
                else
                {
                    bool has_unnecessary = false;
                    for (const auto& d : r_diags)
                    {
                        if (d.is_unnecessary()) { has_unnecessary = true; break; }
                    }
                    const UI::Theme::Color text_col = has_unnecessary
                        ? UI::Theme::Color{115, 120, 130, 170}
                        : surface.m_palette.text_primary;
                    surface.draw_text(device_context, *surface.m_editor_font, lstr, tok_x, cy, text_col);
                }

                // Diagnostics squiggles for right pane
                for (const auto& diag : r_diags)
                {
                    std::size_t start_col = diag.range.start.line == line_index ? diag.range.start.character : 0;
                    std::size_t end_col = diag.range.end.line == line_index ? diag.range.end.character : lstr.size();
                    if (end_col > lstr.size()) end_col = lstr.size();
                    if (start_col >= end_col) end_col = std::min(start_col + 1, lstr.size());

                    float diag_start_x = right_code_x;
                    if (start_col > 0 && start_col <= lstr.size())
                    {
                        diag_start_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, lstr.substr(0, start_col)));
                    }
                    float diag_width = 8.0F;
                    if (end_col > start_col && start_col < lstr.size())
                    {
                        diag_width = static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, lstr.substr(start_col, end_col - start_col)));
                    }

                    const UI::Theme::Color squiggle_color = diag.severity == Language::Protocol::DiagnosticSeverity::Error
                        ? UI::Theme::Color{247, 84, 100, 255}
                        : (diag.severity == Language::Protocol::DiagnosticSeverity::Warning
                            ? UI::Theme::Color{240, 167, 50, 255}
                            : UI::Theme::Color{86, 182, 194, 255});

                    float wave_x = diag_start_x;
                    const float wave_end_x = diag_start_x + std::max(diag_width, 6.0F);
                    const float wave_y = cy + line_height * 0.42F;
                    const float wave_step = 3.0F * scale;
                    const float wave_amp = 1.5F * scale;
                    bool wave_up = true;
                    while (wave_x < wave_end_x)
                    {
                        const float next_x = std::min(wave_x + wave_step, wave_end_x);
                        const float y1 = wave_up ? (wave_y - wave_amp) : (wave_y + wave_amp);
                        const float y2 = wave_up ? (wave_y + wave_amp) : (wave_y - wave_amp);
                        surface.draw_line(device_context,
                            round_to_int(wave_x), round_to_int(y1),
                            round_to_int(next_x), round_to_int(y2),
                            squiggle_color);
                        wave_x = next_x;
                        wave_up = !wave_up;
                    }
                }

                // Right Pane Modern Flat Inline Diagnostic Lens (flex-centered)
                if (!r_diags.empty())
                {
                    const auto* top_diag = &r_diags[0];
                    for (const auto& d : r_diags)
                    {
                        if (d.severity < top_diag->severity)
                        {
                            top_diag = &d;
                        }
                    }

                    UI::Theme::Color badge_bg{44, 20, 26, 210};
                    UI::Theme::Color badge_border{247, 84, 100, 80};
                    UI::Theme::Color badge_fg{255, 120, 135, 255};
                    UI::Theme::Color icon_color{247, 84, 100, 255};
                    std::string_view icon_asset = "diagnostic-error.svg";

                    if (top_diag->severity == Language::Protocol::DiagnosticSeverity::Warning)
                    {
                        badge_bg = UI::Theme::Color{44, 32, 14, 210};
                        badge_border = UI::Theme::Color{240, 167, 50, 80};
                        badge_fg = UI::Theme::Color{250, 188, 80, 255};
                        icon_color = UI::Theme::Color{240, 167, 50, 255};
                        icon_asset = "diagnostic-warning.svg";
                    }
                    else if (top_diag->severity >= Language::Protocol::DiagnosticSeverity::Information)
                    {
                        badge_bg = UI::Theme::Color{18, 34, 46, 210};
                        badge_border = UI::Theme::Color{86, 182, 194, 80};
                        badge_fg = UI::Theme::Color{105, 210, 225, 255};
                        icon_color = UI::Theme::Color{86, 182, 194, 255};
                        icon_asset = "diagnostic-info.svg";
                    }

                    std::string msg = top_diag->message;
                    for (char& ch : msg) { if (ch == '\r' || ch == '\n') ch = ' '; }

                    const int line_text_w = surface.get_text_width(device_context, *surface.m_editor_font, lstr);
                    const bool is_collapsed = (m_split_folding.get_marker(line_index) == UI::Components::FoldMarker::Collapsed);
                    const float lens_start_x = right_code_x + static_cast<float>(line_text_w) + (is_collapsed ? 42.0F * scale : 20.0F * scale);
                    const float avail_w = std::max(right_code_limit - lens_start_x - 16.0F * scale, 80.0F * scale);

                    const float pad_x = 8.0F * scale;
                    const float icon_size = 12.0F * scale;
                    const float gap = 6.0F * scale;

                    int msg_w = surface.get_text_width(device_context, *surface.m_ui_font, msg);
                    const float max_msg_w = std::max(avail_w - (pad_x * 2.0F + icon_size + gap), 40.0F * scale);
                    if (static_cast<float>(msg_w) > max_msg_w && msg.size() > 8)
                    {
                        while (msg.size() > 4 && static_cast<float>(surface.get_text_width(device_context, *surface.m_ui_font, msg + "...")) > max_msg_w)
                        {
                            msg.pop_back();
                        }
                        msg += "...";
                        msg_w = surface.get_text_width(device_context, *surface.m_ui_font, msg);
                    }

                    const float badge_w = pad_x + icon_size + gap + static_cast<float>(msg_w) + pad_x;
                    const float badge_h = 18.0F * scale;
                    const UI::Rect badge_rect{lens_start_x, cy - badge_h * 0.5F, badge_w, badge_h};

                    // 1. Flat Pill Background + Subtle Border
                    surface.fill_rounded_rectangle(device_context, badge_rect, badge_bg, 4.0F * scale);
                    surface.draw_rectangle(device_context, badge_rect, badge_border);

                    // 2. Vector SVG Icon (flex-centered)
                    const int icon_cx = round_to_int(lens_start_x + pad_x + icon_size * 0.5F);
                    const int icon_cy = round_to_int(cy);
                    surface.draw_svg_icon(
                        device_context, icon_asset,
                        icon_cx, icon_cy,
                        std::max(round_to_int(icon_size), 10),
                        icon_color, badge_bg);

                    // 3. Message Text (flex-centered vertically)
                    const float text_x = lens_start_x + pad_x + icon_size + gap;
                    surface.draw_text(device_context, *surface.m_ui_font, msg, text_x, cy, badge_fg);
                }

                // Right Pane Collapsed code placeholder badge (...)
                if (m_split_folding.get_marker(line_index) == UI::Components::FoldMarker::Collapsed)
                {
                    const int line_text_w = surface.get_text_width(device_context, *surface.m_editor_font, lstr);
                    const float badge_x = right_code_x + static_cast<float>(line_text_w) + 8.0F * scale;
                    const float badge_w = 26.0F * scale;
                    const float badge_h = 16.0F * scale;
                    const UI::Rect badge_rect{badge_x, cy - badge_h * 0.5F, badge_w, badge_h};
                    surface.fill_rounded_rectangle(device_context, badge_rect, UI::Theme::Color{45, 50, 65, 230}, 3.0F * scale);
                    surface.draw_rectangle(device_context, badge_rect, UI::Theme::Color{75, 84, 110, 255});
                    surface.draw_text(device_context, *surface.m_small_font, "...", badge_x + 6.0F * scale, cy, UI::Theme::Color{210, 215, 230, 255});
                }

                // Caret for right pane
                if (m_focused && m_caret_blink.is_visible() && m_focused_pane == SplitPaneFocus::Right)
                {
                    for (const auto& cur : split_doc.get_all_cursors())
                    {
                        if (cur.line == line_index)
                        {
                            const std::string_view prefix = lstr.substr(0, std::min(cur.column, lstr.size()));
                            const int caret_x = round_to_int(
                                right_code_x + static_cast<float>(surface.get_text_width(
                                    device_context, *surface.m_editor_font, prefix)));
                            surface.draw_line(
                                device_context,
                                caret_x,
                                round_to_int(cy - 8.0F * scale),
                                caret_x,
                                round_to_int(cy + 8.0F * scale),
                                surface.m_palette.text_primary);
                        }
                    }
                }
            }

            RestoreDC(device_context, -1);
        }
    }

    // Render completion popup overlay if active (VS Code Style)
    std::lock_guard<std::mutex> lsp_lock(m_lsp_mutex);
    if (m_completion_popup.is_visible() && m_completion_popup.get_item_count() > 0 && document != nullptr)
    {
        const std::string_view current_line = document->get_line(document->get_caret_line());
        const std::string_view prefix = current_line.substr(0, std::min(document->get_caret_column(), current_line.size()));
        const float caret_screen_x = code_x + static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, prefix));
        const float caret_line_y = layout.editor_bounds.y + static_cast<float>(physical_line_to_visual_row(m_folding, document->get_caret_line(), document->get_line_count()) - m_scrollbar.get_first_visible_line() + 1) * (20.0F * surface.m_dpi_scale);

        const float item_h = 20.0F * surface.m_dpi_scale;
        const std::size_t count = m_completion_popup.get_item_count();
        const std::size_t scroll_offset = m_completion_popup.get_scroll_offset();
        const std::size_t max_visible = m_completion_popup.get_max_visible_items();
        const std::size_t visible_count = std::min<std::size_t>(count, max_visible);
        const float popup_h = static_cast<float>(visible_count) * item_h + 4.0F * surface.m_dpi_scale; // Compact, no footer

        // Calculate dynamic popup width
        float max_label_w = 220.0F * surface.m_dpi_scale;
        for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count; ++i)
        {
            if (const auto* it = m_completion_popup.get_item(scroll_offset + i))
            {
                const int w = surface.get_text_width(device_context, *surface.m_ui_font, it->label);
                max_label_w = std::max(max_label_w, static_cast<float>(w) + 64.0F * surface.m_dpi_scale);
            }
        }
        const float popup_w = std::clamp(max_label_w, 220.0F * surface.m_dpi_scale, 380.0F * surface.m_dpi_scale);

        const float popup_x = std::clamp(caret_screen_x, layout.editor_bounds.x + 10.0F, std::max(layout.editor_bounds.x + 10.0F, layout.editor_bounds.right() - (popup_w + 20.0F)));
        const float popup_y = std::clamp(caret_line_y, layout.editor_bounds.y + 10.0F, std::max(layout.editor_bounds.y + 10.0F, layout.editor_bounds.bottom() - (popup_h + 20.0F)));

        const UI::Rect actual_bounds{popup_x, popup_y, popup_w, popup_h};

        const UI::Theme::Color completion_bg{24, 24, 28, 255};
        const UI::Theme::Color completion_border{55, 55, 62, 255};
        const UI::Theme::Color completion_selection{0, 95, 184, 255};
        const UI::Theme::Color completion_text_muted{133, 133, 133, 255};

        surface.fill_rounded_rectangle(device_context, actual_bounds, completion_bg, 3.0F * surface.m_dpi_scale);
        surface.draw_rectangle(device_context, actual_bounds, completion_border);

        const std::size_t selected = m_completion_popup.get_selected_index();

        for (std::size_t i = 0; i < max_visible && (scroll_offset + i) < count; ++i)
        {
            const std::size_t item_idx = scroll_offset + i;
            const auto* item = m_completion_popup.get_item(item_idx);
            if (item == nullptr) continue;

            const float row_y = actual_bounds.y + 2.0F + static_cast<float>(i) * item_h;
            const float item_w = actual_bounds.width - (count > max_visible ? 10.0F : 4.0F) * surface.m_dpi_scale;
            const UI::Rect item_rect{actual_bounds.x + 2.0F, row_y, item_w, item_h};

            if (item_idx == selected)
            {
                surface.fill_rounded_rectangle(device_context, item_rect, completion_selection, 2.0F * surface.m_dpi_scale);
            }

            // VS Code Minimalist Kind Icon Badges & Colors
            std::string kind_badge = " ";
            UI::Theme::Color badge_color = surface.m_palette.accent;
            bool is_snippet = false;

            switch (item->kind)
            {
            case Language::Protocol::CompletionItemKind::Snippet:
                kind_badge = "[]";
                badge_color = UI::Theme::Color{79, 193, 255, 255}; // Cyan Snippet Outline
                is_snippet = true;
                break;
            case Language::Protocol::CompletionItemKind::Keyword:
                kind_badge = "{}";
                badge_color = UI::Theme::Color{197, 134, 192, 255}; // Purple Keyword
                break;
            case Language::Protocol::CompletionItemKind::Function:
            case Language::Protocol::CompletionItemKind::Method:
                kind_badge = "f";
                badge_color = UI::Theme::Color{177, 128, 215, 255}; // Lavender Function
                break;
            case Language::Protocol::CompletionItemKind::Variable:
            case Language::Protocol::CompletionItemKind::Field:
                kind_badge = "v";
                badge_color = UI::Theme::Color{156, 220, 254, 255}; // Light Blue Variable
                break;
            case Language::Protocol::CompletionItemKind::Property:
                kind_badge = "p";
                badge_color = UI::Theme::Color{79, 193, 255, 255}; // Cyan Property
                break;
            case Language::Protocol::CompletionItemKind::Class:
            case Language::Protocol::CompletionItemKind::Struct:
            case Language::Protocol::CompletionItemKind::Interface:
                kind_badge = "c";
                badge_color = UI::Theme::Color{78, 201, 176, 255}; // Teal Class
                break;
            case Language::Protocol::CompletionItemKind::File:
                kind_badge = "h";
                badge_color = UI::Theme::Color{156, 220, 254, 255}; // Light Blue File Glyph
                break;
            case Language::Protocol::CompletionItemKind::Module:
                kind_badge = "m";
                badge_color = UI::Theme::Color{220, 220, 170, 255}; // Yellow Module
                break;
            default:
                kind_badge = "abc";
                badge_color = surface.m_palette.text_muted;
                break;
            }

            // Draw Icon Badge
            surface.draw_text(device_context, *surface.m_ui_font, kind_badge, item_rect.x + 4.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, badge_color);

            // Draw Item Label
            const UI::Theme::Color label_color = (item_idx == selected) ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_primary;
            surface.draw_text(device_context, *surface.m_editor_font, item->label, item_rect.x + 24.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, label_color);

            // Draw Right-aligned Snippet Enter Icon (Image 1: ↵)
            if (is_snippet)
            {
                surface.draw_text(device_context, *surface.m_ui_font, "<-", item_rect.right() - 18.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, (item_idx == selected) ? UI::Theme::Color{255, 255, 255, 255} : completion_text_muted);
            }
            else if (!item->detail.empty())
            {
                const int detail_w = surface.get_text_width(device_context, *surface.m_ui_font, item->detail);
                const float detail_x = std::max(item_rect.x + 180.0F * surface.m_dpi_scale, item_rect.right() - static_cast<float>(detail_w) - 6.0F * surface.m_dpi_scale);
                surface.draw_text(device_context, *surface.m_ui_font, item->detail, detail_x, row_y + item_h * 0.5F, completion_text_muted);
            }
        }

        // Draw Minimal Vertical Scrollbar Thumb if popup is scrollable (Image 2)
        if (count > max_visible)
        {
            const float track_x = actual_bounds.right() - 4.0F * surface.m_dpi_scale;
            const float track_y = actual_bounds.y + 2.0F;
            const float track_h = static_cast<float>(visible_count) * item_h;
            const float thumb_h = std::max(12.0F * surface.m_dpi_scale, track_h * (static_cast<float>(max_visible) / static_cast<float>(count)));
            const float max_scroll = static_cast<float>(count - max_visible);
            const float thumb_y = track_y + (static_cast<float>(scroll_offset) / max_scroll) * (track_h - thumb_h);

            const UI::Rect thumb_rect{track_x, thumb_y, 3.0F * surface.m_dpi_scale, thumb_h};
            surface.fill_rounded_rectangle(device_context, thumb_rect, UI::Theme::Color{90, 90, 96, 255}, 1.5F * surface.m_dpi_scale);
        }

        // Render Detail / Documentation Info Popup (VS Code Style Flyout Card)
        const auto* selected_item = m_completion_popup.get_selected_item();
        if (selected_item != nullptr && (!selected_item->detail.empty() || !selected_item->documentation.empty() || selected_item->kind != Language::Protocol::CompletionItemKind::Text))
        {
            const float detail_pad = 8.0F * surface.m_dpi_scale;
            const float detail_w = 340.0F * surface.m_dpi_scale;

            // Check if there is enough space on the right, otherwise place on the left
            float detail_x = actual_bounds.right() + 4.0F * surface.m_dpi_scale;
            if (detail_x + detail_w > layout.editor_bounds.right() - 8.0F)
            {
                detail_x = actual_bounds.x - detail_w - 4.0F * surface.m_dpi_scale;
                if (detail_x < layout.editor_bounds.x + 8.0F)
                {
                    detail_x = std::max(layout.editor_bounds.x + 8.0F, layout.editor_bounds.right() - detail_w - 8.0F);
                }
            }

            // Word wrap documentation lines
            std::vector<std::string> doc_lines;
            if (!selected_item->documentation.empty())
            {
                std::stringstream ss(selected_item->documentation);
                std::string line;
                while (std::getline(ss, line))
                {
                    if (line.empty())
                    {
                        doc_lines.push_back("");
                        continue;
                    }
                    std::stringstream words_ss(line);
                    std::string word;
                    std::string current_wrapped;
                    while (words_ss >> word)
                    {
                        std::string test = current_wrapped.empty() ? word : current_wrapped + " " + word;
                        int text_w = surface.get_text_width(device_context, *surface.m_ui_font, test);
                        if (static_cast<float>(text_w) > (detail_w - detail_pad * 2.0F - 4.0F) && !current_wrapped.empty())
                        {
                            doc_lines.push_back(current_wrapped);
                            current_wrapped = word;
                        }
                        else
                        {
                            current_wrapped = test;
                        }
                    }
                    if (!current_wrapped.empty())
                    {
                        doc_lines.push_back(current_wrapped);
                    }
                }
            }

            const float header_h = 24.0F * surface.m_dpi_scale;
            const float line_spacing = 16.0F * surface.m_dpi_scale;
            const float content_h = header_h + (doc_lines.empty() ? 6.0F * surface.m_dpi_scale : (6.0F * surface.m_dpi_scale + static_cast<float>(doc_lines.size()) * line_spacing + detail_pad));
            const float detail_h = std::clamp(std::max(actual_bounds.height, content_h), 48.0F * surface.m_dpi_scale, 300.0F * surface.m_dpi_scale);
            const float detail_y = actual_bounds.y;

            const UI::Rect detail_bounds{detail_x, detail_y, detail_w, detail_h};

            // Background & Border
            surface.fill_rounded_rectangle(device_context, detail_bounds, UI::Theme::Color{22, 22, 26, 255}, 3.0F * surface.m_dpi_scale);
            surface.draw_rectangle(device_context, detail_bounds, completion_border);

            // Top Header: Kind Badge + Label + Signature/Detail
            float cursor_x = detail_bounds.x + detail_pad;
            const float cursor_y = detail_bounds.y + header_h * 0.5F;

            // Header Kind Badge
            std::string kind_badge = " ";
            UI::Theme::Color badge_color = surface.m_palette.accent;
            std::string kind_name;

            switch (selected_item->kind)
            {
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

            surface.draw_text(device_context, *surface.m_ui_font, kind_badge, cursor_x, cursor_y, badge_color);
            cursor_x += 20.0F * surface.m_dpi_scale;

            std::string header_text = selected_item->detail.empty() ? (kind_name + " " + selected_item->label) : selected_item->detail;
            surface.draw_text(device_context, *surface.m_editor_font, header_text, cursor_x, cursor_y, UI::Theme::Color{230, 230, 235, 255});

            // Divider line
            const float sep_y = detail_bounds.y + header_h;
            surface.draw_line(device_context, round_to_int(detail_bounds.x), round_to_int(sep_y), round_to_int(detail_bounds.right()), round_to_int(sep_y), completion_border);

            // Documentation text lines
            float doc_y = sep_y + 10.0F * surface.m_dpi_scale;
            bool inside_code_block = false;
            for (const auto& doc_line : doc_lines)
            {
                if (doc_y + line_spacing * 0.5F > detail_bounds.bottom() - 4.0F) break;
                if (doc_line.starts_with("```"))
                {
                    inside_code_block = !inside_code_block;
                    continue;
                }
                if (!doc_line.empty())
                {
                    if (doc_line.starts_with("### "))
                    {
                        surface.draw_text(device_context, *surface.m_ui_font, doc_line.substr(4), detail_bounds.x + detail_pad, doc_y, UI::Theme::Color{156, 220, 254, 255});
                    }
                    else if (inside_code_block)
                    {
                        surface.draw_text(device_context, *surface.m_editor_font, doc_line, detail_bounds.x + detail_pad + 6.0F * surface.m_dpi_scale, doc_y, UI::Theme::Color{230, 230, 240, 255});
                    }
                    else if (doc_line.starts_with("- "))
                    {
                        surface.draw_text(device_context, *surface.m_ui_font, "• " + doc_line.substr(2), detail_bounds.x + detail_pad + 4.0F * surface.m_dpi_scale, doc_y, UI::Theme::Color{210, 210, 215, 255});
                    }
                    else
                    {
                        surface.draw_text(device_context, *surface.m_ui_font, doc_line, detail_bounds.x + detail_pad, doc_y, UI::Theme::Color{204, 204, 204, 255});
                    }
                }
                doc_y += line_spacing;
            }
        }
    }

    // Render Parameter Hint / Signature Help tooltip (e.g. add_compile_options(<option> ..))
    if (m_signature_help.is_visible() && !m_signature_help.get_help().signatures.empty() && document != nullptr)
    {
        const std::string_view current_line = document->get_line(document->get_caret_line());
        const std::size_t caret_col = document->get_caret_column();
        const std::string_view prefix = current_line.substr(0, std::min(caret_col, current_line.size()));

        std::size_t open_count = 0;
        std::size_t close_count = 0;
        for (char ch : prefix)
        {
            if (ch == '(') ++open_count;
            else if (ch == ')') ++close_count;
        }

        if (open_count <= close_count)
        {
            m_signature_help.hide();
        }
        else
        {
            const auto& sig = m_signature_help.get_help().signatures[0];
            const float line_h = 20.0F * surface.m_dpi_scale;
            const float caret_screen_x = code_x + static_cast<float>(surface.get_text_width(device_context, *surface.m_editor_font, prefix));
            const float line_top_y = layout.editor_bounds.y + static_cast<float>(physical_line_to_visual_row(m_folding, document->get_caret_line(), document->get_line_count()) - m_scrollbar.get_first_visible_line()) * line_h;

            const int text_w = surface.get_text_width(device_context, *surface.m_editor_font, sig.label);
            const float hint_w = static_cast<float>(text_w) + 16.0F * surface.m_dpi_scale;
            const float hint_h = 22.0F * surface.m_dpi_scale;
            const float hint_x = std::clamp(caret_screen_x, layout.editor_bounds.x + 10.0F, std::max(layout.editor_bounds.x + 10.0F, layout.editor_bounds.right() - (hint_w + 20.0F)));

            const bool popup_visible = (m_completion_popup.is_visible() && m_completion_popup.get_item_count() > 0);
            float hint_y = line_top_y - hint_h - 3.0F * surface.m_dpi_scale;

            if (hint_y < layout.editor_bounds.y + 2.0F)
            {
                if (popup_visible)
                {
                    // If completion popup is visible below the line, never overlap it!
                    // Place signature help either right at the top edge above line or below the whole completion popup
                    const std::size_t count = m_completion_popup.get_item_count();
                    const std::size_t max_visible = m_completion_popup.get_max_visible_items();
                    const std::size_t visible_count = std::min<std::size_t>(count, max_visible);
                    const float popup_h = static_cast<float>(visible_count) * line_h + 4.0F * surface.m_dpi_scale;
                    const float popup_bottom = line_top_y + line_h + popup_h + 4.0F * surface.m_dpi_scale;

                    if (line_top_y >= layout.editor_bounds.y + 12.0F)
                    {
                        hint_y = std::max(layout.editor_bounds.y + 2.0F, line_top_y - hint_h - 1.0F);
                    }
                    else if (popup_bottom + hint_h < layout.editor_bounds.bottom() - 10.0F)
                    {
                        hint_y = popup_bottom;
                    }
                    else
                    {
                        hint_y = layout.editor_bounds.y + 2.0F;
                    }
                }
                else
                {
                    hint_y = line_top_y + line_h + 2.0F * surface.m_dpi_scale;
                }
            }

            const UI::Rect hint_bounds{hint_x, hint_y, hint_w, hint_h};

            // Dark background with subtle border
            const UI::Theme::Color hint_bg{24, 24, 30, 255};
            const UI::Theme::Color hint_border{55, 55, 68, 255};
            surface.fill_rounded_rectangle(device_context, hint_bounds, hint_bg, 3.0F * surface.m_dpi_scale);
            surface.draw_rectangle(device_context, hint_bounds, hint_border);

            // Parameter hint text in teal/cyan editor font
            surface.draw_text(device_context, *surface.m_editor_font, sig.label, hint_bounds.x + 8.0F * surface.m_dpi_scale, hint_bounds.y + hint_h * 0.5F, UI::Theme::Color{78, 201, 176, 255});
        }
    }
}

void TextEditor::render_overlays(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    draw_split_drop_overlay(surface, device_context, layout);
    draw_tab_action_menu(surface, device_context, layout);
    draw_diagnostic_hover_overlay(surface, device_context, layout);
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const
{
    const float scale = surface.m_dpi_scale;
    const bool is_split_active = m_is_split && m_split_document_index.has_value() && *m_split_document_index < m_controller.get_documents().size();
    const float splitter_x = layout.editor_bounds.x + (layout.editor_bounds.width - 2.0F * scale) * m_split_ratio;

    if (is_split_active && m_focused_pane == SplitPaneFocus::Right)
    {
        const UI::Editor::TextDocumentModel* split_doc = m_controller.get_document(*m_split_document_index);
        if (split_doc == nullptr) return {};

        const float line_height = 20.0F * scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        const std::size_t total_lines = split_doc->get_line_count();
        m_split_scrollbar.synchronize(count_visible_lines(m_split_folding, total_lines), visible_count);

        const std::size_t first_line = m_split_scrollbar.get_first_visible_line();
        const float clamped_y = std::clamp(
            point_y, layout.editor_bounds.y, std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
        const std::size_t clicked_row = static_cast<std::size_t>(std::max(
            static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
        const std::size_t line_index = visual_row_to_physical_line(
            m_split_folding, first_line + clicked_row, total_lines);
        const std::string_view line = split_doc->get_line(line_index);

        const float right_x = splitter_x + 2.0F * scale;
        const float right_gutter_w = layout.gutter_bounds.width;
        const float code_x = right_x + right_gutter_w + 14.0F * scale;
        const float target_x = std::max(point_x - code_x, 0.0F);

        std::size_t column = 0;
        int previous_width = 0;
        while (column < line.size())
        {
            const std::size_t next_column = next_character_column(line, column);
            const int next_width = surface.get_text_width(
                device_context, *surface.m_editor_font, line.substr(0, next_column));
            if (target_x < static_cast<float>(previous_width + next_width) * 0.5F)
            {
                break;
            }
            column = next_column;
            previous_width = next_width;
        }
        return {line_index, column};
    }

    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        return {};
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        point_y, layout.editor_bounds.y, std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = visual_row_to_physical_line(
        m_folding, first_line + clicked_row, total_lines);
    const std::string_view line = document->get_line(line_index);
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale - m_text_scroll_offset;
    const float target_x = std::max(point_x - code_x, 0.0F);
    std::size_t column = 0;
    int previous_width = 0;
    while (column < line.size())
    {
        const std::size_t next_column = next_character_column(line, column);
        const int next_width = surface.get_text_width(
            device_context, *surface.m_editor_font, line.substr(0, next_column));
        if (target_x < static_cast<float>(previous_width + next_width) * 0.5F)
        {
            break;
        }
        column = next_column;
        previous_width = next_width;
    }
    return {line_index, column};
}

void TextEditor::draw_diagnostic_hover_overlay(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!m_hovered_diagnostic.has_value())
    {
        return;
    }

    const auto& info = *m_hovered_diagnostic;
    const auto& diag = info.diagnostic;
    const float scale = surface.m_dpi_scale;

    // Severity colors
    UI::Theme::Color accent_color{247, 84, 100, 255}; // Error red
    if (diag.severity == Language::Protocol::DiagnosticSeverity::Warning)
    {
        accent_color = UI::Theme::Color{240, 167, 50, 255}; // Warning amber
    }
    else if (diag.severity >= Language::Protocol::DiagnosticSeverity::Information)
    {
        accent_color = UI::Theme::Color{86, 182, 194, 255}; // Info cyan
    }

    // Prepare diagnostic message & source tag
    std::string msg = diag.message;
    for (char& ch : msg) { if (ch == '\r' || ch == '\n') ch = ' '; }

    std::string source_tag;
    if (!diag.source.empty())
    {
        source_tag = diag.source;
        if (!diag.code.empty())
        {
            source_tag += "(" + diag.code + ")";
        }
    }
    else if (!diag.code.empty())
    {
        source_tag = "clangd(" + diag.code + ")";
    }
    else
    {
        source_tag = "clangd";
    }

    const int msg_w = surface.get_text_width(device_context, *surface.m_ui_font, msg);
    const int src_w = surface.get_text_width(device_context, *surface.m_small_font, source_tag);

    const float card_padding = 14.0F * scale;
    const float max_w = std::max(60.0F * scale, std::min(layout.editor_bounds.width - 40.0F * scale, 640.0F * scale));
    const float min_w = std::min(380.0F * scale, max_w);
    const float card_w = std::clamp(static_cast<float>(msg_w + src_w) + 48.0F * scale, min_w, max_w);

    const bool has_symbol = !info.symbol_name.empty();
    const float card_h = has_symbol ? (98.0F * scale) : (74.0F * scale);

    // Calculate popup position (prefer above the anchor, fall back to below if too close to top)
    float card_y = info.anchor_y - card_h - 6.0F * scale;
    if (card_y < layout.editor_bounds.y + 4.0F * scale)
    {
        card_y = info.anchor_y + 24.0F * scale;
    }
    const float min_card_x = layout.editor_bounds.x + 8.0F * scale;
    const float max_card_x = std::max(min_card_x, layout.editor_bounds.right() - card_w - 8.0F * scale);
    const float card_x = std::clamp(info.anchor_x - 30.0F * scale, min_card_x, max_card_x);

    const UI::Rect card_rect{card_x, card_y, card_w, card_h};

    // 1. Drop Shadow
    surface.fill_rounded_rectangle(
        device_context,
        UI::Rect{card_x + 2.0F * scale, card_y + 4.0F * scale, card_w, card_h},
        UI::Theme::Color{0, 0, 0, 130},
        6.0F * scale);

    // 2. Card Background & Outline
    surface.fill_rounded_rectangle(
        device_context,
        card_rect,
        UI::Theme::Color{26, 28, 36, 252},
        6.0F * scale);
    surface.draw_rectangle(
        device_context,
        card_rect,
        UI::Theme::Color{58, 62, 78, 255});

    // Left accent strip (colored line matching error/warning)
    surface.fill_rounded_rectangle(
        device_context,
        UI::Rect{card_x, card_y + 4.0F * scale, 3.0F * scale, card_h - 8.0F * scale},
        accent_color,
        1.5F * scale);

    // 3. Header row: Message + Source Tag
    const float row1_y = card_y + 16.0F * scale;
    const float max_msg_w = card_w - static_cast<float>(src_w) - 40.0F * scale;
    std::string display_msg = msg;
    if (static_cast<float>(msg_w) > max_msg_w && display_msg.size() > 10)
    {
        while (display_msg.size() > 6 && static_cast<float>(surface.get_text_width(device_context, *surface.m_ui_font, display_msg + "...")) > max_msg_w)
        {
            display_msg.pop_back();
        }
        display_msg += "...";
    }

    surface.draw_text(
        device_context,
        *surface.m_ui_font,
        display_msg,
        card_x + card_padding,
        row1_y,
        UI::Theme::Color{230, 235, 245, 255});

    const float src_x = card_x + card_w - static_cast<float>(src_w) - card_padding;
    surface.draw_text(
        device_context,
        *surface.m_small_font,
        source_tag,
        src_x,
        row1_y,
        UI::Theme::Color{80, 155, 245, 255});

    // Divider Line
    const float div_y = card_y + 32.0F * scale;
    surface.draw_line(
        device_context,
        round_to_int(card_x + 10.0F * scale),
        round_to_int(div_y),
        round_to_int(card_x + card_w - 10.0F * scale),
        round_to_int(div_y),
        UI::Theme::Color{46, 50, 64, 255});

    // 4. Middle row (if symbol / include info exists)
    float actions_y = card_y + 48.0F * scale;
    if (has_symbol)
    {
        const float sym_row_y = card_y + 45.0F * scale;
        const int sym_text_w = surface.get_text_width(device_context, *surface.m_small_font, info.symbol_name);
        const float sym_pill_w = static_cast<float>(sym_text_w) + 12.0F * scale;
        const float sym_pill_h = 16.0F * scale;
        const UI::Rect sym_pill_rect{card_x + card_padding, sym_row_y - sym_pill_h * 0.5F, sym_pill_w, sym_pill_h};

        surface.fill_rounded_rectangle(
            device_context,
            sym_pill_rect,
            UI::Theme::Color{42, 45, 58, 255},
            3.0F * scale);
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            info.symbol_name,
            sym_pill_rect.x + 6.0F * scale,
            sym_row_y,
            UI::Theme::Color{240, 242, 250, 255});

        // Detail / Include path info
        const float detail_x = sym_pill_rect.right() + 8.0F * scale;
        std::string detail_text = info.line_text;
        while (!detail_text.empty() && (detail_text.front() == ' ' || detail_text.front() == '\t')) detail_text.erase(detail_text.begin());
        const float max_det_w = card_x + card_w - detail_x - card_padding;
        if (static_cast<float>(surface.get_text_width(device_context, *surface.m_small_font, detail_text)) > max_det_w && detail_text.size() > 10)
        {
            while (detail_text.size() > 6 && static_cast<float>(surface.get_text_width(device_context, *surface.m_small_font, detail_text + "...")) > max_det_w)
            {
                detail_text.pop_back();
            }
            detail_text += "...";
        }
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            detail_text,
            detail_x,
            sym_row_y,
            UI::Theme::Color{150, 156, 172, 255});

        actions_y = card_y + 74.0F * scale;
    }

    // 5. Bottom Action Bar: [Explain and Fix (Ctrl+Shift+.)]  View Problem (Alt+F8)  Quick Fix... (Ctrl+.)
    const std::string btn_label = "Explain and Fix (Ctrl+Shift+.)";
    const int btn_text_w = surface.get_text_width(device_context, *surface.m_small_font, btn_label);
    const float btn_w = static_cast<float>(btn_text_w) + 16.0F * scale;
    const float btn_h = 20.0F * scale;
    const UI::Rect btn_rect{card_x + card_padding, actions_y - btn_h * 0.5F, btn_w, btn_h};

    surface.fill_rounded_rectangle(
        device_context,
        btn_rect,
        UI::Theme::Color{0, 122, 204, 255}, // VS Code primary blue button
        3.0F * scale);
    surface.draw_text(
        device_context,
        *surface.m_small_font,
        btn_label,
        btn_rect.x + 8.0F * scale,
        actions_y,
        UI::Theme::Color{255, 255, 255, 255});

    // Secondary Action Links
    float link_x = btn_rect.right() + 12.0F * scale;
    const std::string link1 = "View Problem (Alt+F8)";
    surface.draw_text(
        device_context,
        *surface.m_small_font,
        link1,
        link_x,
        actions_y,
        UI::Theme::Color{80, 160, 250, 255});

    link_x += static_cast<float>(surface.get_text_width(device_context, *surface.m_small_font, link1)) + 14.0F * scale;
    const std::string link2 = "Quick Fix... (Ctrl+.)";
    surface.draw_text(
        device_context,
        *surface.m_small_font,
        link2,
        link_x,
        actions_y,
        UI::Theme::Color{80, 160, 250, 255});
}

} // namespace Zenvra::Platform::Win32::Components
