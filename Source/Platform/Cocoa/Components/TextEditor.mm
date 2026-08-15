#include "Platform/Cocoa/Components/TextEditor.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Zenvra::Platform::Cocoa::Components
{

namespace
{

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

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

std::size_t visual_row_to_physical_line(const UI::Components::EditorFoldingModel& folding,
                                        std::size_t visual_row,
                                        std::size_t total_lines)
{
    std::size_t current_visual = 0;
    for (std::size_t i = 0; i < total_lines; ++i)
    {
        if (!folding.is_line_hidden(i))
        {
            if (current_visual == visual_row) return i;
            ++current_visual;
        }
    }
    return total_lines > 0 ? total_lines - 1 : 0;
}

std::size_t physical_line_to_visual_row(const UI::Components::EditorFoldingModel& folding,
                                        std::size_t physical_line,
                                        std::size_t total_lines)
{
    std::size_t visual_row = 0;
    for (std::size_t i = 0; i < physical_line && i < total_lines; ++i)
    {
        if (!folding.is_line_hidden(i)) ++visual_row;
    }
    return visual_row;
}

std::size_t count_visible_lines(const UI::Components::EditorFoldingModel& folding,
                                std::size_t total_lines)
{
    std::size_t visible = 0;
    for (std::size_t i = 0; i < total_lines; ++i)
    {
        if (!folding.is_line_hidden(i)) ++visible;
    }
    return visible;
}

std::optional<std::size_t> fold_start_line_at_point(
    const UI::Components::EditorFoldingModel& folding,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x, float point_y, float dpi_scale,
    std::size_t first_visual_row, std::size_t total_lines)
{
    const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * dpi_scale;
    const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
    if (!layout.gutter_bounds.contains(point_x, point_y) || point_x < fold_margin_left)
    {
        return std::nullopt;
    }
    const float line_height = 20.0F * dpi_scale;
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((point_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = visual_row_to_physical_line(
        folding, first_visual_row + clicked_row, total_lines);
    if (!folding.is_fold_start(line_index))
    {
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
    const int search_limit = std::max(0, static_cast<int>(start_line) - 500);

    for (int line_idx = static_cast<int>(start_line); line_idx >= search_limit; --line_idx)
    {
        const std::string_view line = document.get_line(static_cast<std::size_t>(line_idx));

        int search_end = static_cast<int>(line.size());
        if (line_idx == static_cast<int>(start_line))
        {
            search_end = std::min(static_cast<int>(start_col) + 1, search_end);
        }

        for (int i = search_end - 1; i >= 0; --i)
        {
            if (line[i] == '}')
            {
                ++brace_depth;
            }
            else if (line[i] == '{')
            {
                if (brace_depth > 0)
                {
                    --brace_depth;
                }
                else
                {
                    open_brace = UI::Editor::TextPosition{
                        static_cast<std::size_t>(line_idx), static_cast<std::size_t>(i)};
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
            const std::string_view line = document.get_line(line_idx);
            const std::size_t search_start = (line_idx == open_brace->line) ? open_brace->column : 0;

            for (std::size_t i = search_start; i < line.size(); ++i)
            {
                if (line[i] == '{')
                {
                    ++brace_depth;
                }
                else if (line[i] == '}')
                {
                    --brace_depth;
                    if (brace_depth == 0)
                    {
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
    }
    return opened;
}

std::size_t TextEditor::open_dropped_paths(std::span<const std::filesystem::path> dropped_paths)
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
    }
    return created;
}

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py, bool extend, int clicks,
    std::string& cmd_out)
{
    // Tab bar interaction
    for (std::size_t i = 0; i < m_tab_count; ++i)
    {
        if (m_tab_bounds[i].contains(px, py))
        {
            const float close_width =
                UI::Editor::StudioEditorMetrics::editor_tab_close_width * surface.m_dpi_scale;
            const UI::Rect close_bounds{
                m_tab_bounds[i].right() - close_width, m_tab_bounds[i].y,
                close_width, m_tab_bounds[i].height};
            m_focused = true;
            if (close_bounds.contains(px, py))
            {
                const bool closed = m_controller.close_file(i);
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
            if (m_controller.activate_file(i))
            {
                m_scrollbar.reset();
                m_reveal_caret_pending = true;
                m_caret_blink.reset();
            }
            m_tab_drag_drop.begin_drag(i, px);
            m_drag_initial_tab_x = m_tab_bounds[i].x;
            return true;
        }
    }

    UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));

    // Minimap
    if (document != nullptr && m_minimap.is_point(layout, px, py))
    {
        m_scrollbar.synchronize(count_visible_lines(m_folding, document->get_line_count()), visible_count);
        if (const auto line = m_minimap.handle_pointer_press(layout, px, py,
                document->get_line_count(), visible_count,
                m_scrollbar.get_first_visible_line()))
        {
            static_cast<void>(m_scrollbar.scroll_to(*line));
        }
        m_focused = true;
        m_pointer_selecting = false;
        m_reveal_caret_pending = false;
        m_caret_blink.reset();
        return true;
    }
    // Scrollbar
    if (document != nullptr && m_scrollbar.is_point(layout, px, py))
    {
        m_scrollbar.synchronize(count_visible_lines(m_folding, document->get_line_count()), visible_count);
        m_focused = true;
        m_pointer_selecting = false;
        m_reveal_caret_pending = false;
        m_caret_blink.reset();
        return m_scrollbar.handle_pointer_press(layout, px, py);
    }

    // Empty state buttons
    if (document == nullptr)
    {
        if (layout.editor_bounds.contains(px, py))
        {
            if (m_empty_state_open_btn.handle_pointer_press(px, py))
            {
                cmd_out = "zde.project.open";
                return true;
            }
            if (m_empty_state_clone_btn.handle_pointer_press(px, py))
            {
                cmd_out = "zde.git.clone";
                return true;
            }
        }
    }

    if ((!layout.gutter_bounds.contains(px, py) && !layout.editor_bounds.contains(px, py)) ||
        document == nullptr)
    {
        return false;
    }

    const std::size_t total_lines = document->get_line_count();
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);

    // Fold toggle
    if (const std::optional<std::size_t> fold_line = fold_start_line_at_point(
            m_folding, layout, px, py, surface.m_dpi_scale,
            m_scrollbar.get_first_visible_line(), total_lines))
    {
        m_folding.toggle_fold(*fold_line);
        m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
        m_reveal_caret_pending = true;
        return true;
    }

    m_focused = true;
    m_pointer_selecting = true;
    const UI::Editor::TextPosition position = position_from_point(surface, layout, px, py);
    if (clicks >= 2)
    {
        document->select_word_at(position.line, position.column);
    }
    else
    {
        static_cast<void>(document->set_caret(position.line, position.column, extend));
    }
    m_reveal_caret_pending = true;
    m_caret_blink.reset();
    return true;
}

bool TextEditor::is_tab_interactive_point(
    const StudioWorkspaceRenderer&, const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) const noexcept
{
    return layout.tab_bar_bounds.contains(px, py);
}

bool TextEditor::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout, float px, float py) noexcept
{
    std::optional<std::size_t> new_hovered;
    std::optional<std::size_t> new_close_hovered;
    const float close_width = 28.0F * layout.dpi_scale;
    for (std::size_t i = 0; i < m_tab_count; ++i)
    {
        if (m_tab_bounds[i].contains(px, py))
        {
            new_hovered = i;
            if (px >= m_tab_bounds[i].right() - close_width)
            {
                new_close_hovered = i;
            }
            break;
        }
    }
    bool changed = new_hovered != m_hovered_tab_index;
    m_hovered_tab_index = new_hovered;
    changed |= new_close_hovered != m_hovered_tab_close_index;
    m_hovered_tab_close_index = new_close_hovered;

    changed |= m_scrollbar.set_hovered(layout, px, py);

    // Empty state button hover
    changed |= m_empty_state_open_btn.handle_pointer_move(px, py);
    changed |= m_empty_state_clone_btn.handle_pointer_move(px, py);

    // Fold margin hover
    std::optional<std::size_t> new_fold_line;
    if (const UI::Editor::TextDocumentModel* doc = m_controller.get_active_document())
    {
        const float line_height = 20.0F;
        const std::size_t total_lines = doc->get_line_count();
        const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width;
        const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
        if (layout.gutter_bounds.contains(px, py) && px >= fold_margin_left)
        {
            const std::size_t clicked_row = static_cast<std::size_t>(std::max(
                static_cast<int>((py - layout.editor_bounds.y) / line_height), 0));
            const std::size_t line_index = visual_row_to_physical_line(
                m_folding, m_scrollbar.get_first_visible_line() + clicked_row, total_lines);
            if (m_folding.is_fold_start(line_index))
            {
                new_fold_line = line_index;
            }
        }
    }
    changed |= new_fold_line != m_hovered_fold_line;
    m_hovered_fold_line = new_fold_line;
    return changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py)
{
    if (m_tab_drag_drop.is_dragging())
    {
        m_tab_drag_drop.drag(px);
        return true;
    }
    if (m_scrollbar.handle_pointer_drag(layout, py)) return true;
    if (const UI::Editor::TextDocumentModel* doc = m_controller.get_active_document())
    {
        const float line_height = 20.0F * surface.m_dpi_scale;
        const std::size_t visible_count = static_cast<std::size_t>(std::max(
            static_cast<int>(layout.editor_bounds.height / line_height), 1));
        m_scrollbar.synchronize(count_visible_lines(m_folding, doc->get_line_count()), visible_count);
        if (const auto line = m_minimap.handle_pointer_drag(layout, py,
                doc->get_line_count(), visible_count,
                m_scrollbar.get_first_visible_line()))
        {
            m_scrollbar.scroll_to(*line);
            return true;
        }
    }
    if (m_pointer_selecting)
    {
        auto pos = position_from_point(surface, layout, px, py);
        if (auto* doc = m_controller.get_active_document())
        {
            doc->set_caret(pos.line, pos.column, true);
        }
        return true;
    }
    return false;
}

bool TextEditor::handle_pointer_release() noexcept
{
    m_pointer_selecting = false;
    if (m_tab_drag_drop.is_dragging())
    {
        m_tab_drag_drop.end_drag();
        return true;
    }
    bool r = m_scrollbar.handle_pointer_release();
    r |= m_minimap.handle_pointer_release();
    return r;
}

bool TextEditor::handle_scroll(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py, std::string& cmd_out,
    std::ptrdiff_t delta, bool horiz) noexcept
{
    (void)cmd_out;
    if (layout.tab_bar_bounds.contains(px, py))
    {
        const float speed = 32.0F * layout.dpi_scale;
        m_tab_scroll_offset += static_cast<float>(delta) * speed;
        m_tab_scroll_offset = std::clamp(m_tab_scroll_offset, 0.0f, m_max_tab_scroll);
        return true;
    }
    if (horiz)
    {
        if (layout.editor_bounds.contains(px, py))
        {
            const float speed = 32.0F * layout.dpi_scale;
            m_text_scroll_offset += static_cast<float>(delta) * speed;
            m_text_scroll_offset = std::clamp(m_text_scroll_offset, 0.0f, m_max_text_scroll);
            return true;
        }
        return false;
    }
    if (m_scrollbar.is_point(layout, px, py) ||
        layout.editor_bounds.contains(px, py) ||
        layout.gutter_bounds.contains(px, py) ||
        m_minimap.is_point(layout, px, py))
    {
        if (const UI::Editor::TextDocumentModel* document = m_controller.get_active_document())
        {
            const float line_height = 20.0F * surface.m_dpi_scale;
            const std::size_t visible_count = static_cast<std::size_t>(std::max(
                static_cast<int>(layout.editor_bounds.height / line_height), 1));
            m_scrollbar.synchronize(document->get_line_count(), visible_count);
        }
        m_reveal_caret_pending = false;
        return m_scrollbar.scroll_lines(delta);
    }
    return false;
}

bool TextEditor::handle_input(UI::Editor::EditorInputCommand cmd, bool extend) { return m_controller.execute_input(cmd, extend); }
bool TextEditor::handle_action(UI::Editor::EditorAction action) { return m_controller.execute_action(action); }
std::optional<bool> TextEditor::handle_command(std::string_view id) {
    auto action = UI::Editor::EditorController::action_from_command_id(id);
    if (!action) return std::nullopt;
    return m_controller.execute_action(*action);
}
std::optional<bool> TextEditor::is_command_enabled(std::string_view id) const noexcept {
    auto action = UI::Editor::EditorController::action_from_command_id(id);
    if (!action) return std::nullopt;
    return m_controller.can_execute_action(*action);
}
bool TextEditor::handle_text_input(std::string_view utf8) { return m_controller.insert_text(utf8); }
bool TextEditor::is_focused() const noexcept { return m_focused; }
bool TextEditor::is_empty_state_interactive_point(float px, float py) const noexcept {
    return m_empty_state_open_btn.get_bounds().contains(px, py) ||
           m_empty_state_clone_btn.get_bounds().contains(px, py);
}
bool TextEditor::is_scrollbar_point(const UI::Editor::StudioEditorLayoutResult& l, float px, float py) const noexcept {
    return m_scrollbar.is_point(l, px, py);
}
bool TextEditor::is_minimap_point(const UI::Editor::StudioEditorLayoutResult& l, float px, float py) const noexcept {
    return m_minimap.is_point(l, px, py);
}
bool TextEditor::is_fold_margin_point(
    const StudioWorkspaceRenderer&, const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) const noexcept
{
    const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * layout.dpi_scale;
    const float fold_margin_left = layout.gutter_bounds.right() - fold_margin;
    return layout.gutter_bounds.contains(px, py) && px >= fold_margin_left;
}

bool TextEditor::tick_animations() noexcept
{
    bool needs_redraw = m_focused && m_controller.get_active_document() != nullptr && m_caret_blink.tick();

    // Lerp animated tab positions (mirrors X11 TextEditor.cpp)
    bool animating = false;
    for (auto& [doc, animated_x] : m_tab_animated_x)
    {
        if (m_tab_target_x.contains(doc))
        {
            const float target_x = m_tab_target_x[doc];
            if (std::abs(animated_x - target_x) > 0.5F)
            {
                animated_x += (target_x - animated_x) * 0.3F;
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

const UI::Editor::TextDocumentModel* TextEditor::get_document() const noexcept { return m_controller.get_active_document(); }

void TextEditor::render(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    draw_tab_strip(surface, context, layout);
    draw_document(surface, context, layout);
}

void TextEditor::draw_tab_strip(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const std::span<const UI::Editor::EditorSessionDocument> documents = m_controller.get_documents();

    float total_width = 0.0F;
    for (std::size_t index = 0; index < documents.size(); ++index) {
        total_width += UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.m_ui_font->getTextWidth(
                std::string{documents[index].text.get_file_name()})),
            surface.m_dpi_scale);
        total_width += UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }

    m_max_tab_scroll = std::max(0.0F, total_width - layout.tab_bar_bounds.width);
    if (m_max_tab_scroll == 0.0F) {
        const_cast<TextEditor*>(this)->m_tab_scroll_offset = 0.0F;
    }

    m_tab_count = 0;
    float tab_x = layout.tab_bar_bounds.x - m_tab_scroll_offset;
    const float right_limit = layout.tab_bar_bounds.right();
    const std::optional<std::size_t> active_index = m_controller.get_active_index();

    for (std::size_t index = 0; index < documents.size(); ++index) {
        const UI::Editor::TextDocumentModel& document = documents[index].text;
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.m_ui_font->getTextWidth(
                std::string{document.get_file_name()})),
            surface.m_dpi_scale);
        if (tab_x > right_limit) {
            break;
        }
        UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};

        if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == index) {
            bounds.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
        }
        if (m_tab_count < max_visible_tabs) {
            m_tab_bounds[m_tab_count] = bounds;
            ++m_tab_count;
        }
        tab_x += width + UI::Editor::StudioEditorMetrics::editor_tab_gap * surface.m_dpi_scale;
    }

    auto draw_single_tab = [&](std::size_t tab_index) {
        const std::size_t index = tab_index;
        const UI::Editor::TextDocumentModel& document = documents[index].text;
        const bool active = active_index && *active_index == index;
        const UI::Rect& bounds = m_tab_bounds[tab_index];
        const bool close_hovered = m_hovered_tab_close_index && *m_hovered_tab_close_index == tab_index;
        const bool tab_hovered = m_hovered_tab_index && *m_hovered_tab_index == tab_index;

        surface.fill_rectangle(context, bounds,
            active ? surface.m_colors.tab_active_background :
            tab_hovered ? surface.m_colors.active_line_background :
            surface.m_colors.tab_background);

        const int tab_left = round_to_int(bounds.x);
        const int tab_right = round_to_int(bounds.right()) - 1;
        const int tab_top = round_to_int(bounds.y);
        const int tab_bottom = round_to_int(bounds.bottom()) - 1;

        surface.draw_line(context, tab_left, tab_top, tab_right, tab_top, surface.m_colors.border);
        surface.draw_line(context, tab_left, tab_top, tab_left, tab_bottom, surface.m_colors.border);
        surface.draw_line(context, tab_right, tab_top, tab_right, tab_bottom, surface.m_colors.border);

        const std::string icon_asset = UI::Editor::file_icon_asset_for_path(
            std::filesystem::path{std::string{document.get_file_name()}});
        surface.draw_svg_icon(context, "Assets/icons/" + icon_asset,
            round_to_int(bounds.x + (UI::Editor::StudioEditorMetrics::editor_tab_icon_offset + 4.0F) * surface.m_dpi_scale),
            round_to_int(bounds.y + bounds.height * 0.5F),
            std::max(round_to_int(14.0F * surface.m_dpi_scale), 10),
            active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background,
            false);

        const float text_x = bounds.x + UI::Editor::StudioEditorMetrics::editor_tab_label_offset * surface.m_dpi_scale;
        const float center_y = bounds.y + bounds.height * 0.5F;
        surface.draw_text(context, *surface.m_ui_font, document.get_file_name(), text_x, center_y,
            active ? surface.m_text.primary : surface.m_text.muted, &layout.tab_bar_bounds);

        const float close_cx = bounds.right() -
            UI::Editor::StudioEditorMetrics::editor_tab_close_width * 0.5F * surface.m_dpi_scale;
        if (close_hovered) {
            surface.draw_svg_icon(context, "Assets/icons/close-minimal.svg",
                round_to_int(close_cx), round_to_int(center_y),
                std::max(round_to_int(11.0F * surface.m_dpi_scale), 9),
                active ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);
        } else if (document.is_dirty()) {
            surface.draw_svg_icon(context, "Assets/icons/dirty.svg",
                round_to_int(close_cx), round_to_int(center_y),
                std::max(round_to_int(10.0F * surface.m_dpi_scale), 8),
                surface.m_palette.warning,
                active ? surface.m_palette.tab_active_background : surface.m_palette.tab_background);
        }
    };

    surface.push_clip(context, layout.tab_bar_bounds);
    for (std::size_t tab_index = 0; tab_index < m_tab_count; ++tab_index) {
        if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == tab_index)
            continue;
        draw_single_tab(tab_index);
    }
    if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() < m_tab_count) {
        draw_single_tab(m_tab_drag_drop.get_dragged_index());
    }
    surface.pop_clip(context);

    const int tab_bar_bottom = round_to_int(layout.tab_bar_bounds.bottom()) - 1;
    const int tab_bar_left = round_to_int(layout.tab_bar_bounds.x);
    const int tab_bar_right = round_to_int(layout.tab_bar_bounds.right());

    if (active_index && *active_index < m_tab_count) {
        const UI::Rect& active_bounds = m_tab_bounds[*active_index];
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
        const float thumb_x = layout.tab_bar_bounds.x +
            (m_tab_scroll_offset / m_max_tab_scroll) * (track_width - thumb_width);
        const UI::Rect thumb_bounds{
            thumb_x, layout.tab_bar_bounds.bottom() - 3.0F * surface.m_dpi_scale,
            thumb_width, 3.0F * surface.m_dpi_scale};

        CGFloat thumb_rgba[4];
        StudioWorkspaceRenderer::color_to_rgba(
            m_hovered_tab_scrollbar ? surface.m_palette.text_primary : surface.m_palette.text_muted,
            thumb_rgba);
        surface.fill_rectangle(context, thumb_bounds, thumb_rgba);
    }
}

void TextEditor::draw_document(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Editor::TextDocumentModel* document = m_controller.get_active_document();
    if (document == nullptr)
    {
        draw_empty_state(surface, context, layout);
        return;
    }

    const float dpi = surface.m_dpi_scale;
    const float line_height = 20.0F * dpi;
    const float first_center_y = layout.editor_bounds.y + line_height * 0.5F;
    const float code_x = layout.editor_bounds.x + 14.0F * dpi - m_text_scroll_offset;
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
        m_reveal_caret_pending = false;
    }
    const std::size_t first_visual_row = m_scrollbar.get_first_visible_line();
    const std::size_t first_line = visual_row_to_physical_line(m_folding, first_visual_row, total_lines);
    const bool syntax_highlighting = UI::Editor::supports_editor_syntax_highlighting(document->get_file_name());

    // Gutter separator + indent guides (VS Code style)
    {
        const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * dpi;
        const float gutter_line_x = layout.gutter_bounds.right() - fold_margin - 1.0F;
        surface.draw_line(context,
            round_to_int(gutter_line_x),
            round_to_int(layout.gutter_bounds.y),
            round_to_int(gutter_line_x),
            round_to_int(layout.gutter_bounds.bottom()),
            surface.m_colors.border);

        const float space_width = static_cast<float>(surface.m_editor_font->getTextWidth(" "));
        const UI::Components::ActiveIndentScope active_scope =
            m_folding.get_active_indent_scope(document->get_caret_line(), tab_size);

        std::size_t row_guide = 0;
        for (std::size_t line_index = first_line; row_guide < visible_count && line_index < total_lines; ++line_index)
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

                surface.draw_line(context,
                    round_to_int(guide_x), round_to_int(y_top),
                    round_to_int(guide_x), round_to_int(y_bottom),
                    is_active ? surface.m_colors.indent_guide_active : surface.m_colors.indent_guide);
            }
        }
    }

    // Pass 1: Gutter and backgrounds
    std::size_t row_pass1 = 0;
    for (std::size_t line_index = first_line; row_pass1 < visible_count && line_index < total_lines; ++line_index)
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
            surface.fill_rectangle(context,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                         layout.editor_bounds.right() - layout.gutter_bounds.x, line_height},
                surface.m_colors.active_line_background);
        }

        const float fold_margin = UI::Editor::StudioEditorMetrics::fold_margin_width * dpi;
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - fold_margin - 4.0F * dpi -
            static_cast<float>(surface.m_small_font->getTextWidth(number));
        surface.draw_text(context, *surface.m_small_font, number, number_x, center_y,
            active_line ? surface.m_text.primary : surface.m_text.muted);

        if (has_gutter_marker(line))
        {
            const int marker_x = round_to_int(layout.gutter_bounds.right() - 13.0F * dpi);
            const int marker_y = round_to_int(center_y);
            const int half = std::max(round_to_int(3.0F * dpi), 2);
            surface.draw_line(context, marker_x, marker_y - half, marker_x + half, marker_y, surface.m_colors.text_muted);
            surface.draw_line(context, marker_x + half, marker_y, marker_x, marker_y + half, surface.m_colors.text_muted);
            surface.draw_line(context, marker_x, marker_y + half, marker_x - half, marker_y, surface.m_colors.text_muted);
            surface.draw_line(context, marker_x - half, marker_y, marker_x, marker_y - half, surface.m_colors.text_muted);
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
            const int box_half = std::max(round_to_int(5.0F * dpi), 4);
            surface.fill_rectangle(context,
                UI::Rect{static_cast<float>(fold_cx - box_half), static_cast<float>(fold_cy - box_half),
                         static_cast<float>(box_half * 2), static_cast<float>(box_half * 2)},
                active_line ? surface.m_colors.active_line_background : surface.m_colors.editor_background);
            surface.draw_rectangle(context,
                UI::Rect{static_cast<float>(fold_cx - box_half), static_cast<float>(fold_cy - box_half),
                         static_cast<float>(box_half * 2), static_cast<float>(box_half * 2)},
                fold_hovered ? surface.m_colors.accent : surface.m_colors.border);

            const int sign_inset = std::max(round_to_int(2.0F * dpi), 2);
            surface.draw_line(context,
                fold_cx - box_half + sign_inset, fold_cy,
                fold_cx + box_half - sign_inset, fold_cy,
                fold_hovered ? surface.m_colors.accent : surface.m_colors.text_muted);
            if (fold_marker == UI::Components::FoldMarker::Collapsed)
            {
                surface.draw_line(context,
                    fold_cx, fold_cy - box_half + sign_inset,
                    fold_cx, fold_cy + box_half - sign_inset,
                    fold_hovered ? surface.m_colors.accent : surface.m_colors.text_muted);
            }
        }
        else if (fold_marker == UI::Components::FoldMarker::Continuation)
        {
            surface.draw_line(context,
                fold_cx, round_to_int(center_y - line_height * 0.5F),
                fold_cx, round_to_int(center_y + line_height * 0.5F),
                surface.m_colors.border);
        }
        else if (fold_marker == UI::Components::FoldMarker::End)
        {
            surface.draw_line(context,
                fold_cx, round_to_int(center_y - line_height * 0.5F),
                fold_cx, fold_cy,
                surface.m_colors.border);
            surface.draw_line(context,
                fold_cx, fold_cy,
                fold_cx + round_to_int(fold_margin * 0.35F), fold_cy,
                surface.m_colors.border);
        }

        if (fold_marker == UI::Components::FoldMarker::Expanded)
        {
            const int box_half = std::max(round_to_int(5.0F * dpi), 4);
            surface.draw_line(context,
                fold_cx, fold_cy + box_half,
                fold_cx, round_to_int(center_y + line_height * 0.5F),
                surface.m_colors.border);
        }
    }

    // Pass 2: Selection (animated) + text rendering with clipping
    const float hscroll_height = (m_max_text_scroll > 0.0f) ? 14.0F * dpi : 0.0f;
    surface.push_clip(context, UI::Rect{
        layout.editor_bounds.x, layout.editor_bounds.y,
        layout.editor_bounds.width,
        std::max(0.0f, layout.editor_bounds.height - hscroll_height)});
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

                const float selection_x = static_cast<float>(surface.m_editor_font->getTextWidth(
                    std::string{line.substr(0, selection_start)}));
                float selection_width = static_cast<float>(surface.m_editor_font->getTextWidth(
                    std::string{line.substr(selection_start, selection_end - selection_start)}));

                if (line_index < selection.end.line)
                {
                    selection_width += 6.0F * dpi;
                }

                selection_targets.push_back(UI::Rect{
                    selection_x,
                    static_cast<float>(physical_line_to_visual_row(m_folding, line_index, total_lines)) * line_height,
                    selection_width,
                    line_height});
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
            if (screen_y + anim_rect.height >= layout.editor_bounds.y && screen_y <= layout.editor_bounds.bottom())
            {
                const int snap_y = round_to_int(screen_y);
                const int snap_bottom = round_to_int(screen_y + anim_rect.height);
                const int snap_x = round_to_int(screen_x);
                const int snap_right = round_to_int(screen_x + anim_rect.width);
                surface.fill_rounded_rectangle(context,
                    UI::Rect{static_cast<float>(snap_x), static_cast<float>(snap_y),
                             static_cast<float>(snap_right - snap_x), static_cast<float>(snap_bottom - snap_y)},
                    surface.m_colors.selection_background, 4.0F * dpi);
            }
        }
    }

    float max_line_width = 0.0F;
    std::size_t row_pass2 = 0;
    for (std::size_t line_index = first_line; row_pass2 < visible_count && line_index < total_lines; ++line_index)
    {
        if (m_folding.is_line_hidden(line_index))
        {
            continue;
        }
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row_pass2) * line_height;
        ++row_pass2;
        const bool active_line = line_index == document->get_caret_line();

        const float current_line_width = static_cast<float>(surface.m_editor_font->getTextWidth(std::string{line}));
        max_line_width = std::max(max_line_width, current_line_width);

        const auto token_color = [&surface](UI::Editor::EditorTokenKind kind) -> const std::string& {
            switch (kind)
            {
            case UI::Editor::EditorTokenKind::Keyword: return surface.m_text.keyword;
            case UI::Editor::EditorTokenKind::Number: return surface.m_text.number;
            case UI::Editor::EditorTokenKind::Label: return surface.m_text.label;
            case UI::Editor::EditorTokenKind::Type: return surface.m_text.type;
            case UI::Editor::EditorTokenKind::Comment: return surface.m_text.comment;
            case UI::Editor::EditorTokenKind::String: return surface.m_text.success;
            case UI::Editor::EditorTokenKind::Plain: return surface.m_text.primary;
            }
            return surface.m_text.primary;
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
                    if (auto open_pos = m_brace_animation.get_open_brace();
                        open_pos && open_pos->line == line_index &&
                        open_pos->column >= rendered_bytes &&
                        open_pos->column < rendered_bytes + token.text.size())
                    {
                        has_animated_brace = true;
                        brace_offset = open_pos->column - rendered_bytes;
                    }
                    if (auto close_pos = m_brace_animation.get_close_brace();
                        close_pos && close_pos->line == line_index &&
                        close_pos->column >= rendered_bytes &&
                        close_pos->column < rendered_bytes + token.text.size())
                    {
                        has_animated_brace = true;
                        brace_offset = close_pos->column - rendered_bytes;
                    }
                }

                if (has_animated_brace)
                {
                    if (brace_offset > 0)
                    {
                        const std::string_view pre = token.text.substr(0, brace_offset);
                        surface.draw_text(context, *surface.m_editor_font, pre, token_x, center_y, token_color(token.kind));
                        token_x += static_cast<float>(surface.m_editor_font->getTextWidth(std::string{pre}));
                    }

                    const std::string_view brace_char = token.text.substr(brace_offset, 1);
                    const float pulse = m_brace_animation.get_pulse_scale();
                    const float brace_w = static_cast<float>(surface.m_editor_font->getTextWidth(std::string{brace_char}));
                    const float extra_w = (brace_w * pulse - brace_w) * 0.5F;
                    const float extra_h = (line_height * pulse - line_height) * 0.5F;
                    const float screen_y = center_y - line_height * 0.5F;

                    UI::Theme::Color pulse_color = surface.m_palette.selection_background;
                    pulse_color.red = std::min(static_cast<unsigned int>(pulse_color.red) + 30U, 255U);
                    pulse_color.green = std::min(static_cast<unsigned int>(pulse_color.green) + 30U, 255U);
                    pulse_color.blue = std::min(static_cast<unsigned int>(pulse_color.blue) + 30U, 255U);
                    CGFloat rgba[4];
                    StudioWorkspaceRenderer::color_to_rgba(pulse_color, rgba);
                    surface.fill_rounded_rectangle(context,
                        UI::Rect{token_x - extra_w - 2.0F, screen_y - extra_h,
                                 brace_w + extra_w * 2.0F + 4.0F, line_height + extra_h * 2.0F},
                        rgba, 3.0F * dpi * pulse);

                    surface.draw_text(context, *surface.m_editor_font, brace_char,
                        token_x, center_y, surface.m_text.accent);
                    token_x += brace_w;

                    if (brace_offset + 1 < token.text.size())
                    {
                        const std::string_view post = token.text.substr(brace_offset + 1);
                        surface.draw_text(context, *surface.m_editor_font, post, token_x, center_y, token_color(token.kind));
                        token_x += static_cast<float>(surface.m_editor_font->getTextWidth(std::string{post}));
                    }
                }
                else
                {
                    surface.draw_text(context, *surface.m_editor_font, token.text,
                        token_x, center_y, token_color(token.kind));
                    token_x += static_cast<float>(surface.m_editor_font->getTextWidth(std::string{token.text}));
                }
                rendered_bytes += token.text.size();
            }
            if (rendered_bytes < line.size())
            {
                surface.draw_text(context, *surface.m_editor_font,
                    line.substr(rendered_bytes), token_x, center_y, surface.m_text.primary);
            }
        }
        else
        {
            surface.draw_text(context, *surface.m_editor_font, line, code_x, center_y, surface.m_text.primary);
        }

        // Caret
        if (m_focused && m_caret_blink.is_visible())
        {
            for (const auto& cur : document->get_all_cursors())
            {
                if (cur.line == line_index)
                {
                    const std::string_view prefix = line.substr(0, std::min(cur.column, line.size()));
                    const int caret_x = round_to_int(
                        code_x + static_cast<float>(surface.m_editor_font->getTextWidth(std::string{prefix})));
                    surface.draw_line(context,
                        caret_x, round_to_int(center_y - 8.0F * dpi),
                        caret_x, round_to_int(center_y + 8.0F * dpi),
                        surface.m_colors.text_primary);
                }
            }
        }
    }

    surface.pop_clip(context);

    // Horizontal scroll thumb
    const float content_width = max_line_width + 28.0F * dpi;
    const float new_max_scroll = std::max(0.0F, content_width - layout.editor_bounds.width);
    if (new_max_scroll > m_max_text_scroll)
    {
        m_max_text_scroll = new_max_scroll;
    }
    else if (new_max_scroll < m_max_text_scroll * 0.8F)
    {
        m_max_text_scroll = new_max_scroll;
    }
    if (m_max_text_scroll == 0.0F)
    {
        const_cast<TextEditor*>(this)->m_text_scroll_offset = 0.0F;
    }
    else if (m_text_scroll_offset > m_max_text_scroll)
    {
        const_cast<TextEditor*>(this)->m_text_scroll_offset = m_max_text_scroll;
    }

    if (m_max_text_scroll > 0.0F)
    {
        const float track_width = layout.editor_bounds.width;
        const float track_height = 14.0F * dpi;
        const float track_y = layout.editor_bounds.bottom() - track_height;
        const float thumb_width = std::max(20.0F * dpi, track_width * (track_width / content_width));
        const float thumb_x = layout.editor_bounds.x +
            (m_text_scroll_offset / m_max_text_scroll) * (track_width - thumb_width);
        const float thumb_height = 6.0F * dpi;
        surface.fill_rectangle(context,
            UI::Rect{thumb_x, track_y + (track_height - thumb_height) * 0.5F,
                     thumb_width, thumb_height},
            surface.m_colors.text_muted);
    }

    // Render scrollbar and minimap
    m_scrollbar.render(surface, context, layout);
    if (document)
    {
        m_minimap.render(surface, context, layout, *document, first_line, visible_count);
    }
}

void TextEditor::draw_empty_state(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
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

    surface.draw_png_image(context, "Assets/icons/zenvra_logo.png", center_x, current_y + logo_size / 2, logo_size);
    current_y += logo_size + gap1;

    if (surface.m_large_font)
    {
        const std::string title = "Zenvra Development Studio";
        const int title_w = surface.m_large_font->getTextWidth(title);
        surface.draw_text(context, *surface.m_large_font, title,
            static_cast<float>(center_x - title_w / 2), static_cast<float>(current_y),
            surface.m_text.primary);
    }
    current_y += gap2;

    const float btn_w = 300.0F * dpi;
    const float btn_h = 40.0F * dpi;
    const float btn_x = center_x - btn_w * 0.5F;

    // Open Folder Button
    m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
    const auto& open_state = m_empty_state_open_btn.get_state();
    const UI::Theme::Color open_base = surface.m_palette.accent;
    const UI::Theme::Color open_bg = open_state.pressed ? surface.m_palette.border :
        (open_state.hovered ? UI::Theme::Color{
            static_cast<std::uint8_t>(std::min(255, open_base.red + 24)),
            static_cast<std::uint8_t>(std::min(255, open_base.green + 24)),
            static_cast<std::uint8_t>(std::min(255, open_base.blue + 24)),
            open_base.alpha}
        : open_base);
    CGFloat open_rgba[4];
    StudioWorkspaceRenderer::color_to_rgba(open_bg, open_rgba);
    surface.fill_rounded_rectangle(context, m_empty_state_open_btn.get_bounds(), open_rgba, 4.0F * dpi);

    const float icon_size = 16.0F * dpi;
    const float open_text_w = static_cast<float>(surface.m_ui_font->getTextWidth("Open Folder"));
    const float open_icon_x = btn_x + btn_w * 0.5F - (icon_size + 8.0F * dpi + open_text_w) * 0.5F;
    surface.draw_text(context, *surface.m_ui_font, "Open Folder",
        open_icon_x + icon_size + 8.0F * dpi,
        static_cast<float>(current_y) + btn_h * 0.5F,
        surface.m_text.primary);
    surface.draw_svg_icon(context, "Assets/icons/folder.svg",
        round_to_int(open_icon_x + icon_size * 0.5F),
        round_to_int(static_cast<float>(current_y) + btn_h * 0.5F),
        round_to_int(icon_size), surface.m_palette.text_primary, open_bg);

    current_y += round_to_int(btn_h) + round_to_int(10.0F * dpi);

    // Clone Repository Button
    m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, static_cast<float>(current_y), btn_w, btn_h});
    const auto& clone_state = m_empty_state_clone_btn.get_state();
    const UI::Theme::Color clone_bg = clone_state.pressed ? surface.m_palette.border :
        (clone_state.hovered ? surface.m_palette.hover_background : surface.m_palette.editor_background);
    CGFloat clone_rgba[4];
    StudioWorkspaceRenderer::color_to_rgba(clone_bg, clone_rgba);
    surface.draw_rectangle(context, m_empty_state_clone_btn.get_bounds(), surface.m_colors.border);
    surface.fill_rounded_rectangle(context, m_empty_state_clone_btn.get_bounds(), clone_rgba, 4.0F * dpi);

    const float clone_text_w = static_cast<float>(surface.m_ui_font->getTextWidth("Clone Repository"));
    const float clone_icon_x = btn_x + btn_w * 0.5F - (icon_size + 8.0F * dpi + clone_text_w) * 0.5F;
    surface.draw_text(context, *surface.m_ui_font, "Clone Repository",
        clone_icon_x + icon_size + 8.0F * dpi,
        static_cast<float>(current_y) + btn_h * 0.5F,
        surface.m_text.primary);
    surface.draw_svg_icon(context, "Assets/icons/terminal.svg",
        round_to_int(clone_icon_x + icon_size * 0.5F),
        round_to_int(static_cast<float>(current_y) + btn_h * 0.5F),
        round_to_int(icon_size), surface.m_palette.text_primary, clone_bg);
}

UI::Editor::TextPosition TextEditor::position_from_point(
    const StudioWorkspaceRenderer& surface,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float px, float py) const
{
    const float line_height = 20.0F * surface.m_dpi_scale;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    const std::size_t total_lines = m_controller.get_active_document()
        ? m_controller.get_active_document()->get_line_count() : 0;
    m_scrollbar.synchronize(count_visible_lines(m_folding, total_lines), visible_count);
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        py, layout.editor_bounds.y, std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = visual_row_to_physical_line(
        m_folding, first_line + clicked_row, total_lines);
    const std::string_view line = m_controller.get_active_document()->get_line(line_index);
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale - m_text_scroll_offset;
    const float target_x = std::max(px - code_x, 0.0F);
    std::size_t column = 0;
    int previous_width = 0;
    while (column < line.size())
    {
        const std::size_t next_column = next_character_column(line, column);
        const int next_width = surface.m_editor_font->getTextWidth(std::string{line.substr(0, next_column)});
        if (target_x < static_cast<float>(previous_width + next_width) * 0.5F)
        {
            break;
        }
        column = next_column;
        previous_width = next_width;
    }
    return {line_index, column};
}

} // namespace Zenvra::Platform::Cocoa::Components