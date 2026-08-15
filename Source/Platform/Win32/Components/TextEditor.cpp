#include "Platform/Win32/Components/TextEditor.h"

#include "Language/LanguageServerManager.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Flex.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

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

bool TextEditor::is_fold_margin_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * layout.dpi_scale;
    const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
    return layout.gutter_bounds.contains(point_x, point_y) && point_x >= fold_margin_left;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!layout.tab_bar_bounds.contains(point_x, point_y))
    {
        return false;
    }

    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const float right_limit = layout.tab_bar_bounds.right();
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

    if (hovered_tab != m_hovered_tab_index || hovered_close != m_hovered_tab_close_index)
    {
        m_hovered_tab_index = hovered_tab;
        m_hovered_tab_close_index = hovered_close;
        changed = true;
    }
    return scrollbar_changed || changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y)
{
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
        return true;
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
    if (m_dragging_tab_scrollbar)
    {
        m_dragging_tab_scrollbar = false;
        return true;
    }

    if (m_tab_drag_drop.is_dragging())
    {
        m_tab_drag_drop.end_drag();
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
    const bool minimap_was_dragging = m_minimap.handle_pointer_release();
    const bool scrollbar_was_dragging = m_scrollbar.handle_pointer_release();
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
        m_scrollbar.is_point(layout, event.point_x, event.point_y))
    {
        if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
        {
            const float line_height = 20.0F * surface.m_dpi_scale;
            const std::size_t visible_count = static_cast<std::size_t>(std::max(
                static_cast<int>(layout.editor_bounds.height / line_height), 1));
            m_scrollbar.synchronize(document->get_line_count(), visible_count);
        }
        m_reveal_caret_pending = false;
        return m_scrollbar.scroll_lines(event.delta_y);
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
    return changed;
}

bool TextEditor::handle_action(UI::Editor::EditorAction action)
{
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

std::optional<bool> TextEditor::handle_command(std::string_view command_id)
{
    const std::optional<UI::Editor::EditorAction> action =
        UI::Editor::EditorController::action_from_command_id(command_id);
    return action ? std::optional<bool>{handle_action(*action)} : std::nullopt;
}

std::optional<bool> TextEditor::is_command_enabled(
    std::string_view command_id) const noexcept
{
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

    auto* doc = m_controller.get_active_document();
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

        const bool changed = m_controller.insert_text(pair_text);
        if (changed)
        {
            doc->set_caret(initial_caret_line, initial_caret_col + 1);
            m_reveal_caret_pending = true;
            m_caret_blink.reset();

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

    const bool changed = m_controller.insert_text(utf8_text);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();

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

            const std::string_view current_line = doc->get_line(doc->get_caret_line());
            const std::size_t caret_col = doc->get_caret_column();

            // Extract the active word token before cursor
            std::size_t word_start = std::min(caret_col, current_line.size());
            while (word_start > 0)
            {
                const char c = current_line[word_start - 1];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '#' || c == ':' || c == '~')
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
                                    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '#' || ch == ':' || ch == '~')
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

bool TextEditor::is_focused() const noexcept
{
    return m_focused;
}

bool TextEditor::is_scrollbar_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return m_scrollbar.is_point(layout, point_x, point_y);
}

bool TextEditor::is_minimap_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return m_minimap.is_point(layout, point_x, point_y);
}

bool TextEditor::tick_animations() noexcept
{
    bool needs_redraw = m_focused && m_controller.get_active_document() != nullptr && m_caret_blink.tick();
    
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
    draw_document(surface, device_context, layout);
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
                surface.fill_rectangle(
                    device_context,
                    bounds,
                    active ? surface.m_palette.tab_active_background
                           : (tab_hovered ? surface.m_palette.active_line_background : surface.m_palette.tab_background));
                const UI::Theme::Color& tab_edge_color = surface.m_palette.border;
                const int tab_left = round_to_int(bounds.x);
                const int tab_right = round_to_int(bounds.right()) - 1;
                const int tab_top = round_to_int(bounds.y);
                const int tab_bottom = round_to_int(bounds.bottom()) - 1;
                // Keep one flush top rule and vertical separators; there is no bottom
                // rule, so the titlebar remains visually open below the labels.
                surface.draw_line(device_context, tab_left, tab_top, tab_right, tab_top,
                    tab_edge_color);
                surface.draw_line(device_context, tab_left, tab_top, tab_left, tab_bottom,
                    tab_edge_color);
                surface.draw_line(device_context, tab_right, tab_top, tab_right, tab_bottom,
                    tab_edge_color);
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
                    false);
                surface.draw_text(
                    device_context,
                    *surface.m_ui_font,
                    document.get_file_name(),
                    bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset *
                        surface.m_dpi_scale,
                    bounds.y + bounds.height * 0.5F,
                    active ? surface.m_palette.text_primary : surface.m_palette.text_muted);
                if (close_hovered)
                {
                    surface.draw_svg_icon(
                        device_context,
                        "close-minimal.svg",
                        round_to_int(bounds.right() -
                            UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                                0.5F * surface.m_dpi_scale),
                        round_to_int(bounds.y + bounds.height * 0.5F),
                        std::max(round_to_int(11.0F * surface.m_dpi_scale), 9),
                        active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                        surface.m_palette.tab_background);
                }
                else if (document.is_dirty())
                {
                    surface.draw_svg_icon(
                        device_context,
                        "dirty.svg",
                        round_to_int(bounds.right() -
                            UI::Editor::StudioEditorMetrics::editor_tab_close_width *
                                0.5F * surface.m_dpi_scale),
                        round_to_int(bounds.y + bounds.height * 0.5F),
                        std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
                        surface.m_palette.warning,
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
    const int tab_bar_left = round_to_int(layout.workspace_bounds.x);
    const int tab_bar_right = round_to_int(layout.workspace_bounds.right());
    
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
    if (document == nullptr)
    {
        const float dpi = surface.m_dpi_scale;
        const int center_x = round_to_int(layout.editor_bounds.x + layout.editor_bounds.width * 0.5F);
        
        const int logo_size = round_to_int(150.0F * dpi);
        const int gap1 = round_to_int(30.0F * dpi);
        const int gap2 = round_to_int(40.0F * dpi);
        const int line_height = round_to_int(28.0F * dpi);
        const int total_height = logo_size + gap1 + gap2 + 3 * line_height;
        const float full_height = layout.editor_bounds.height + layout.terminal_panel_bounds.height;
        int current_y = round_to_int(layout.editor_bounds.y + (full_height - total_height) * 0.5F);

        surface.draw_png_icon(device_context, "zenvra_logo.png", center_x, current_y + logo_size / 2, logo_size, surface.m_palette.editor_background);
        current_y += logo_size + gap1;

        if (surface.m_ui_font)
        {
            const std::string title = "Zenvra Development Studio";
            if (surface.m_large_font)
            {
                int title_w = surface.m_large_font->getTextWidth(device_context, title);
                surface.draw_text(device_context, *surface.m_large_font, title, static_cast<float>(center_x - title_w / 2), static_cast<float>(current_y), surface.m_palette.text_primary);
            }
            else
            {
                int title_w = surface.m_ui_font->getTextWidth(device_context, title);
                surface.draw_text(device_context, *surface.m_ui_font, title, static_cast<float>(center_x - title_w / 2), static_cast<float>(current_y), surface.m_palette.text_primary);
            }
            current_y += gap2;

            const float btn_w = 300.0F * dpi;
            const float btn_h = 40.0F * dpi;
            const float btn_x = center_x - btn_w * 0.5F;

            // Open Folder Button
            m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
            const auto& open_state = m_empty_state_open_btn.get_state();
            const UI::Theme::Color open_bg = open_state.pressed ? surface.m_palette.accent : (open_state.hovered ? surface.m_palette.accent : surface.m_palette.accent);
            surface.fill_rounded_rectangle(device_context, m_empty_state_open_btn.get_bounds(), open_bg, 4.0F * dpi);

            const float icon_size = 16.0F * dpi;
            const float open_text_w = static_cast<float>(surface.m_ui_font->getTextWidth(device_context, "Open Folder"));
            std::array<Utility::FlexItem, 2> flex_items_open{
                Utility::FlexItem::fixed(icon_size),
                Utility::FlexItem::fixed(open_text_w)
            };
            Utility::FlexOptions flex_opts;
            flex_opts.axis = Utility::LayoutAxis::Horizontal;
            flex_opts.justify_content = Utility::LayoutJustify::Center;
            flex_opts.align_items = Utility::LayoutAlign::Center;
            flex_opts.gap = 8.0F * dpi;

            Utility::FlexLayoutResult flex_res_open = Utility::Flex::calculate(
                m_empty_state_open_btn.get_bounds(), flex_items_open, flex_opts);

            surface.draw_svg_icon(
                device_context, "Assets/icons/folder.svg",
                round_to_int(flex_res_open.items[0].x + flex_res_open.items[0].width * 0.5F),
                round_to_int(flex_res_open.items[0].y + flex_res_open.items[0].height * 0.5F),
                round_to_int(icon_size), surface.m_palette.text_primary, open_bg);
            
            surface.draw_text(
                device_context, *surface.m_ui_font, "Open Folder",
                flex_res_open.items[1].x,
                flex_res_open.items[1].y + flex_res_open.items[1].height * 0.5F,
                surface.m_palette.text_primary);

            current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);

            // Clone Repository Button
            m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
            const auto& clone_state = m_empty_state_clone_btn.get_state();
            const UI::Theme::Color clone_bg = clone_state.pressed ? surface.m_palette.border : (clone_state.hovered ? surface.m_palette.hover_background : surface.m_palette.editor_background);
            surface.fill_rounded_rectangle(device_context, m_empty_state_clone_btn.get_bounds(), clone_bg, 4.0F * dpi);

            const float clone_text_w = static_cast<float>(surface.m_ui_font->getTextWidth(device_context, "Clone Repository"));
            std::array<Utility::FlexItem, 2> flex_items_clone{
                Utility::FlexItem::fixed(icon_size),
                Utility::FlexItem::fixed(clone_text_w)
            };

            Utility::FlexLayoutResult flex_res_clone = Utility::Flex::calculate(
                m_empty_state_clone_btn.get_bounds(), flex_items_clone, flex_opts);

            surface.draw_svg_icon(
                device_context, "Assets/icons/copy.svg",
                round_to_int(flex_res_clone.items[0].x + flex_res_clone.items[0].width * 0.5F),
                round_to_int(flex_res_clone.items[0].y + flex_res_clone.items[0].height * 0.5F),
                round_to_int(icon_size), surface.m_palette.text_primary, clone_bg);

            surface.draw_text(
                device_context, *surface.m_ui_font, "Clone Repository",
                flex_res_clone.items[1].x,
                flex_res_clone.items[1].y + flex_res_clone.items[1].height * 0.5F,
                surface.m_palette.text_primary);
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

            const std::size_t line_indent = m_folding.get_effective_indent(line_index);
            if (line_indent < tab_size)
            {
                continue;
            }

            const float y_top = center_y - line_height * 0.5F;
            const float y_bottom = center_y + line_height * 0.5F;

            for (std::size_t col = tab_size; col <= line_indent; col += tab_size)
            {
                const float guide_x = code_x + static_cast<float>(col) * space_width;
                if (guide_x < layout.editor_bounds.x || guide_x > layout.editor_bounds.right())
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
        const bool active_line = line_index == document->get_caret_line();
        if (active_line && !document->has_selection())
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                    layout.editor_bounds.right() - layout.gutter_bounds.x, line_height},
                surface.m_palette.active_line_background);
        }
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - fold_margin - 4.0F * surface.m_dpi_scale -
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_small_font, number));
        surface.draw_text(
            device_context,
            *surface.m_small_font,
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

        // --- Fold icon and scope guide rendering ---
        const UI::Components::FoldMarker fold_marker = m_folding.get_marker(line_index);
        const float fold_center_x = layout.gutter_bounds.right() - fold_margin * 0.5F;
        const int fold_cx = round_to_int(fold_center_x);
        const int fold_cy = round_to_int(center_y);

        if (fold_marker == UI::Components::FoldMarker::Expanded ||
            fold_marker == UI::Components::FoldMarker::Collapsed)
        {
            const bool fold_hovered = m_hovered_fold_line && *m_hovered_fold_line == line_index;

            // Draw a small rounded-ish box with +/- sign.
            const int box_half = std::max(round_to_int(5.0F * surface.m_dpi_scale), 4);
            RECT box_rect{
                fold_cx - box_half, fold_cy - box_half,
                fold_cx + box_half, fold_cy + box_half};

            // Box background matches editor background for a clean inset look.
            HBRUSH bg_brush = CreateSolidBrush(to_color_ref(active_line ? surface.m_palette.active_line_background : surface.m_palette.editor_background));
            FillRect(device_context, &box_rect, bg_brush);
            DeleteObject(bg_brush);

            // Box border.
            HPEN box_pen = CreatePen(PS_SOLID, 1, to_color_ref(fold_hovered ? surface.m_palette.accent : surface.m_palette.border));
            HGDIOBJ old_pen = SelectObject(device_context, box_pen);
            HGDIOBJ old_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
            Rectangle(device_context, box_rect.left, box_rect.top, box_rect.right, box_rect.bottom);
            SelectObject(device_context, old_brush);
            SelectObject(device_context, old_pen);
            DeleteObject(box_pen);

            // Horizontal line of +/- (always present).
            const int sign_inset = std::max(round_to_int(2.0F * surface.m_dpi_scale), 2);
            surface.draw_line(device_context,
                fold_cx - box_half + sign_inset, fold_cy,
                fold_cx + box_half - sign_inset, fold_cy,
                fold_hovered ? surface.m_palette.accent : surface.m_palette.text_muted);

            if (fold_marker == UI::Components::FoldMarker::Collapsed)
            {
                // Vertical line of + (only when collapsed).
                surface.draw_line(device_context,
                    fold_cx, fold_cy - box_half + sign_inset,
                    fold_cx, fold_cy + box_half - sign_inset,
                    fold_hovered ? surface.m_palette.accent : surface.m_palette.text_muted);
            }
        }
        else if (fold_marker == UI::Components::FoldMarker::Continuation)
        {
            // Vertical scope guide line.
            surface.draw_line(device_context,
                fold_cx, round_to_int(center_y - line_height * 0.5F),
                fold_cx, round_to_int(center_y + line_height * 0.5F),
                surface.m_palette.border);
        }
        else if (fold_marker == UI::Components::FoldMarker::End)
        {
            // Vertical line from top to center, then horizontal to right (corner).
            surface.draw_line(device_context,
                fold_cx, round_to_int(center_y - line_height * 0.5F),
                fold_cx, fold_cy,
                surface.m_palette.border);
            surface.draw_line(device_context,
                fold_cx, fold_cy,
                fold_cx + round_to_int(fold_margin * 0.35F), fold_cy,
                surface.m_palette.border);
        }

        // Connect expanded marker to continuation line below.
        if (fold_marker == UI::Components::FoldMarker::Expanded)
        {
            const int box_half = std::max(round_to_int(5.0F * surface.m_dpi_scale), 4);
            surface.draw_line(device_context,
                fold_cx, fold_cy + box_half,
                fold_cx, round_to_int(center_y + line_height * 0.5F),
                surface.m_palette.border);
        }
    }

    // Pass 2: Text rendering with clipping
    SaveDC(device_context);
    const float hscroll_height = (m_max_text_scroll > 0.0f) ? 14.0F * surface.m_dpi_scale : 0.0f;
    IntersectClipRect(device_context,
        static_cast<int>(layout.editor_bounds.x),
        static_cast<int>(layout.editor_bounds.y),
        static_cast<int>(layout.editor_bounds.right()),
        static_cast<int>(layout.editor_bounds.bottom() - hscroll_height));

    std::vector<UI::Rect> selection_targets;
    for (const auto& cursor : document->get_all_cursors())
    {
        if (cursor.has_selection())
        {
            const UI::Editor::TextSelection selection = cursor.get_selection();
            const std::size_t start_line = selection.start.line;
            const std::size_t end_line = std::min(selection.end.line, start_line + 1000);
            
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
                        surface.draw_text(device_context, *surface.m_editor_font, pre, token_x, center_y, token_color(token.kind));
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
                        surface.draw_text(device_context, *surface.m_editor_font, post, token_x, center_y, token_color(token.kind));
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
                        token_color(token.kind));
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
            surface.draw_text(
                device_context,
                *surface.m_editor_font,
                line,
                code_x,
                center_y,
                surface.m_palette.text_primary);
        }

        // Render diagnostics squiggles under erroneous tokens
        const auto line_diags = document->get_diagnostics_for_line(line_index);
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

        // Render JetBrains-style Inline Error Lens (Inspection hint at end of line)
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

            std::string badge_prefix = "   x  ";
            UI::Theme::Color badge_fg{247, 84, 100, 220};
            UI::Theme::Color badge_bg{48, 20, 24, 180};
            if (top_diag->severity == Language::Protocol::DiagnosticSeverity::Warning)
            {
                badge_prefix = "   !  ";
                badge_fg = UI::Theme::Color{240, 167, 50, 220};
                badge_bg = UI::Theme::Color{48, 38, 20, 180};
            }
            else if (top_diag->severity >= Language::Protocol::DiagnosticSeverity::Information)
            {
                badge_prefix = "   i  ";
                badge_fg = UI::Theme::Color{86, 182, 194, 220};
                badge_bg = UI::Theme::Color{20, 36, 48, 180};
            }

            std::string hint_text = badge_prefix + top_diag->message;
            if (hint_text.size() > 90)
            {
                hint_text = hint_text.substr(0, 87) + "...";
            }

            const float hint_x = code_x + current_line_width + 20.0F * surface.m_dpi_scale;
            const int hint_w = surface.get_text_width(device_context, *surface.m_ui_font, hint_text);
            const float badge_h = 16.0F * surface.m_dpi_scale;

            surface.fill_rounded_rectangle(device_context,
                UI::Rect{hint_x - 4.0F * surface.m_dpi_scale, center_y - badge_h * 0.5F, static_cast<float>(hint_w) + 8.0F * surface.m_dpi_scale, badge_h},
                badge_bg, 3.0F * surface.m_dpi_scale);

            surface.draw_text(device_context, *surface.m_ui_font, hint_text, hint_x, center_y, badge_fg);
        }
        if (m_focused && m_caret_blink.is_visible())
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

        // VS Code dark minimalist widget background & border
        const UI::Theme::Color vscode_bg{24, 24, 28, 255};
        const UI::Theme::Color vscode_border{55, 55, 62, 255};
        const UI::Theme::Color vscode_selection{0, 95, 184, 255};     // VS Code Accent Blue Highlight
        const UI::Theme::Color vscode_text_muted{133, 133, 133, 255};

        surface.fill_rounded_rectangle(device_context, actual_bounds, vscode_bg, 3.0F * surface.m_dpi_scale);
        surface.draw_rectangle(device_context, actual_bounds, vscode_border);

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
                surface.fill_rounded_rectangle(device_context, item_rect, vscode_selection, 2.0F * surface.m_dpi_scale);
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
            surface.draw_text(device_context, *surface.m_ui_font, kind_badge, item_rect.x + 6.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, badge_color);

            // Draw Item Label
            UI::Theme::Color label_color = surface.m_palette.text_primary;
            if (item_idx == selected)
            {
                label_color = UI::Theme::Color{255, 255, 255, 255};
            }
            else if (is_snippet)
            {
                label_color = UI::Theme::Color{79, 193, 255, 255}; // Cyan snippet text
            }
            surface.draw_text(device_context, *surface.m_ui_font, item->label, item_rect.x + 28.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, label_color);

            // Draw Right-aligned Snippet Enter Icon (Image 1: ↵)
            if (is_snippet)
            {
                surface.draw_text(device_context, *surface.m_ui_font, "<-", item_rect.right() - 18.0F * surface.m_dpi_scale, row_y + item_h * 0.5F, (item_idx == selected) ? UI::Theme::Color{255, 255, 255, 255} : vscode_text_muted);
            }
            else if (!item->detail.empty())
            {
                const int detail_w = surface.get_text_width(device_context, *surface.m_ui_font, item->detail);
                const float detail_x = std::max(item_rect.x + 180.0F * surface.m_dpi_scale, item_rect.right() - static_cast<float>(detail_w) - 6.0F * surface.m_dpi_scale);
                surface.draw_text(device_context, *surface.m_ui_font, item->detail, detail_x, row_y + item_h * 0.5F, vscode_text_muted);
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
            surface.draw_rectangle(device_context, detail_bounds, vscode_border);

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
            surface.draw_line(device_context, round_to_int(detail_bounds.x), round_to_int(sep_y), round_to_int(detail_bounds.right()), round_to_int(sep_y), vscode_border);

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

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const
{
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

} // namespace Zenvra::Platform::Win32::Components
