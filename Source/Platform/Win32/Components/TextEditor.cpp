#include "Platform/Win32/Components/TextEditor.h"

#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "UI/Editor/FileIconModel.h"
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
    }
    return opened;
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
    }
    return created;
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

bool TextEditor::handle_pointer_press(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y,
    bool extend_selection)
{
    if (layout.tab_bar_bounds.contains(point_x, point_y))
    {
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

    if ((!layout.gutter_bounds.contains(point_x, point_y) &&
         !layout.editor_bounds.contains(point_x, point_y)) ||
        document == nullptr)
    {
        return false;
    }
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
    if (hovered_tab != m_hovered_tab_index || hovered_close != m_hovered_tab_close_index)
    {
        m_hovered_tab_index = hovered_tab;
        m_hovered_tab_close_index = hovered_close;
        return true;
    }
    return scrollbar_changed;
}

bool TextEditor::handle_pointer_drag(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y)
{
    if (m_tab_drag_drop.is_dragging())
    {
        const bool changed = m_tab_drag_drop.drag(point_x);
        float tab_x = layout.tab_bar_bounds.x;
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
                    m_controller.reorder_file(m_tab_drag_drop.get_dragged_index(), index);
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
    if (m_tab_drag_drop.is_dragging())
    {
        m_tab_drag_drop.end_drag();
        return true;
    }

    const bool was_selecting = m_pointer_selecting;
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
    if (event.delta_x != 0)
    {
        float speed = 20.0f; // px per delta
        if (layout.tab_bar_bounds.contains(event.point_x, event.point_y))
        {
            m_tab_scroll_offset += static_cast<float>(event.delta_x) * speed;
            if (m_tab_scroll_offset < 0.0f) m_tab_scroll_offset = 0.0f;
            if (m_tab_scroll_offset > m_max_tab_scroll) m_tab_scroll_offset = m_max_tab_scroll;
            return true;
        }
        else if (layout.editor_bounds.contains(event.point_x, event.point_y))
        {
            m_text_scroll_offset += static_cast<float>(event.delta_x) * speed;
            if (m_text_scroll_offset < 0.0f) m_text_scroll_offset = 0.0f;
            if (m_text_scroll_offset > m_max_text_scroll) m_text_scroll_offset = m_max_text_scroll;
            return true;
        }
    }

    if (event.delta_y == 0) return false;

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

bool TextEditor::handle_input(
    UI::Editor::EditorInputCommand command,
    bool extend_selection)
{
    const bool changed = m_focused && m_controller.execute_input(command, extend_selection);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
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
    const bool changed = m_focused && m_controller.insert_text(utf8_text);
    if (changed)
    {
        m_reveal_caret_pending = true;
        m_caret_blink.reset();
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
        const bool active = active_index && *active_index == index;
        const float width = UI::Editor::calculate_editor_tab_width(
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_ui_font, document.get_file_name())),
            surface.m_dpi_scale);
        if (tab_x > right_limit)
        {
            break;
        }
        UI::Rect bounds{tab_x, layout.tab_bar_bounds.y, width, layout.tab_bar_bounds.height};
        m_tab_target_x[&document] = tab_x;
        
        if (m_tab_drag_drop.is_dragging() && m_tab_drag_drop.get_dragged_index() == index)
        {
            bounds.x = m_drag_initial_tab_x + m_tab_drag_drop.get_drag_offset();
        }
        else
        {
            if (!m_tab_animated_x.contains(&document))
            {
                m_tab_animated_x[&document] = tab_x;
            }
            bounds.x = m_tab_animated_x[&document];
        }
        const std::size_t tab_index = m_tab_count;
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
        surface.fill_rectangle(device_context, thumb_bounds, surface.m_palette.accent);
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

        int current_y = round_to_int(layout.editor_bounds.y + (layout.editor_bounds.height - total_height) * 0.5F);

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

            const std::pair<std::string, std::string> shortcuts[] = {
                {"Ctrl+N", "New File"},
                {"Ctrl+S", "Save File"},
                {"Ctrl+W", "Close File"},
                {"Ctrl+P", "Go to File"}
            };
            for (int i = 0; i < 4; ++i)
            {
                int sc_w1 = surface.m_ui_font->getTextWidth(device_context, shortcuts[i].first);
                int sc_x1 = center_x - round_to_int(20.0F * dpi) - sc_w1;
                int sc_x2 = center_x + round_to_int(20.0F * dpi);
                surface.draw_text(device_context, *surface.m_ui_font, shortcuts[i].first, static_cast<float>(sc_x1), static_cast<float>(current_y), surface.m_palette.text_muted);
                surface.draw_text(device_context, *surface.m_ui_font, shortcuts[i].second, static_cast<float>(sc_x2), static_cast<float>(current_y), surface.m_palette.text_muted);
                current_y += line_height;
            }
        }
        return;
    }
    const float line_height = 20.0F * surface.m_dpi_scale;
    const float first_center_y = layout.editor_bounds.y + line_height * 0.5F;
    const float code_x = layout.editor_bounds.x + 14.0F * surface.m_dpi_scale - m_text_scroll_offset;
    const std::size_t visible_count = static_cast<std::size_t>(std::max(
        static_cast<int>(layout.editor_bounds.height / line_height), 1));
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
    if (m_reveal_caret_pending)
    {
        static_cast<void>(m_scrollbar.reveal_line(document->get_caret_line()));
        // If caret is off-screen horizontally, we might want to reveal it too, 
        // but for now we just handle vertical.
        m_reveal_caret_pending = false;
    }
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const std::size_t render_count = std::min(
        visible_count, document->get_line_count() - first_line);
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
    surface.draw_line(
        device_context,
        round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.y),
        round_to_int(layout.gutter_bounds.right() - 1.0F),
        round_to_int(layout.gutter_bounds.bottom()),
        surface.m_palette.border);

    // Pass 1: Gutter and backgrounds
    for (std::size_t row = 0; row < render_count; ++row)
    {
        const std::size_t line_index = first_line + row;
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row) * line_height;
        const bool active_line = line_index == document->get_caret_line();
        if (active_line)
        {
            surface.fill_rectangle(
                device_context,
                UI::Rect{layout.gutter_bounds.x, center_y - line_height * 0.5F,
                    layout.editor_bounds.right() - layout.gutter_bounds.x, line_height},
                surface.m_palette.active_line_background);
        }
        const std::string number = std::to_string(line_index + 1);
        const float number_x = layout.gutter_bounds.right() - 24.0F * surface.m_dpi_scale -
            static_cast<float>(surface.get_text_width(
                device_context, *surface.m_small_font, number));
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            number,
            number_x,
            center_y,
            active_line ? surface.m_palette.text_primary : surface.m_palette.text_muted);
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
    }

    // Pass 2: Text rendering with clipping
    SaveDC(device_context);
    const float hscroll_height = (m_max_text_scroll > 0.0f) ? 14.0F * surface.m_dpi_scale : 0.0f;
    IntersectClipRect(device_context,
        static_cast<int>(layout.editor_bounds.x),
        static_cast<int>(layout.editor_bounds.y),
        static_cast<int>(layout.editor_bounds.right()),
        static_cast<int>(layout.editor_bounds.bottom() - hscroll_height));

    float max_line_width = 0.0f;

    for (std::size_t row = 0; row < render_count; ++row)
    {
        const std::size_t line_index = first_line + row;
        const std::string_view line = document->get_line(line_index);
        const float center_y = first_center_y + static_cast<float>(row) * line_height;
        const bool active_line = line_index == document->get_caret_line();
        
        const float current_line_width = static_cast<float>(surface.get_text_width(
            device_context, *surface.m_editor_font, line));
        if (current_line_width > max_line_width) max_line_width = current_line_width;

        if (document->has_selection())
        {
            const UI::Editor::TextSelection selection = document->get_selection();
            if (line_index >= selection.start.line && line_index <= selection.end.line)
            {
                const std::size_t selection_start = line_index == selection.start.line
                    ? selection.start.column
                    : 0;
                const std::size_t selection_end = line_index == selection.end.line
                    ? selection.end.column
                    : line.size();
                const float selection_x = code_x + static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font,
                    line.substr(0, selection_start)));
                float selection_width = static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font,
                    line.substr(selection_start, selection_end - selection_start)));
                if (line_index < selection.end.line)
                {
                    selection_width += 6.0F * surface.m_dpi_scale;
                }
                
                if (selection_width > 0.0F)
                {
                    surface.fill_rectangle(
                        device_context,
                        UI::Rect{selection_x, center_y - line_height * 0.5F,
                            selection_width, line_height},
                        surface.m_palette.selection_background);
                }
            }
        }
        if (syntax_highlighting)
        {
            float token_x = code_x;
            std::size_t rendered_bytes = 0;
            std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens> tokens{};
            const std::size_t token_count = UI::Editor::tokenize_editor_line(line, tokens);
            for (std::size_t token_index = 0; token_index < token_count; ++token_index)
            {
                const UI::Editor::EditorToken& token = tokens[token_index];
                surface.draw_text(
                    device_context,
                    *surface.m_editor_font,
                    token.text,
                    token_x,
                    center_y,
                    token_color(token.kind));
                token_x += static_cast<float>(surface.get_text_width(
                    device_context, *surface.m_editor_font, token.text));
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
        if (active_line && m_focused && m_caret_blink.is_visible())
        {
            const std::string_view prefix = line.substr(0, document->get_caret_column());
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
    m_scrollbar.synchronize(document->get_line_count(), visible_count);
    const std::size_t first_line = m_scrollbar.get_first_visible_line();
    const float clamped_y = std::clamp(
        point_y, layout.editor_bounds.y, std::max(layout.editor_bounds.bottom() - 1.0F, layout.editor_bounds.y));
    const std::size_t clicked_row = static_cast<std::size_t>(std::max(
        static_cast<int>((clamped_y - layout.editor_bounds.y) / line_height), 0));
    const std::size_t line_index = std::min(
        first_line + clicked_row, document->get_line_count() - 1);
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
