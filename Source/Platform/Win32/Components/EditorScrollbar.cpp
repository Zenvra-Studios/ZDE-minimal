#include "Platform/Win32/Components/EditorScrollbar.h"

#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"

#include <algorithm>

namespace Zenvra::Platform::Win32::Components
{

void EditorScrollbar::reset() noexcept
{
    m_model = UI::Editor::EditorScrollModel{};
    m_hovered = false;
}

void EditorScrollbar::synchronize(
    std::size_t total_lines,
    std::size_t visible_lines) noexcept
{
    static_cast<void>(m_model.set_line_metrics(total_lines, visible_lines));
}

bool EditorScrollbar::scroll_lines(std::ptrdiff_t line_delta) noexcept
{
    return m_model.scroll_lines(line_delta);
}

bool EditorScrollbar::scroll_to(std::size_t first_visible_line) noexcept
{
    return m_model.scroll_to(first_visible_line);
}

bool EditorScrollbar::reveal_line(std::size_t line_index) noexcept
{
    return m_model.reveal_line(line_index);
}

bool EditorScrollbar::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    if (!is_point(layout, point_x, point_y))
    {
        return false;
    }
    static_cast<void>(m_model.begin_pointer_drag(
        point_y, get_track_bounds(layout), 28.0F * layout.dpi_scale));
    return true;
}

bool EditorScrollbar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_y) noexcept
{
    if (!m_model.is_dragging())
    {
        return false;
    }
    static_cast<void>(m_model.drag_pointer(
        point_y, get_track_bounds(layout), 28.0F * layout.dpi_scale));
    return true;
}

bool EditorScrollbar::handle_pointer_release() noexcept
{
    return m_model.end_pointer_drag();
}

bool EditorScrollbar::is_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    return layout.scrollbar_bounds.contains(point_x, point_y);
}

bool EditorScrollbar::set_hovered(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    const bool hovered = is_point(layout, point_x, point_y);
    const bool changed = hovered != m_hovered;
    m_hovered = hovered;
    return changed;
}

std::size_t EditorScrollbar::get_first_visible_line() const noexcept
{
    return m_model.get_first_visible_line();
}

void EditorScrollbar::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    surface.fill_rectangle(
        device_context, layout.scrollbar_bounds, surface.m_palette.editor_background);
    if (m_model.get_maximum_first_line() == 0)
    {
        return;
    }
    
    /**
     * 
     * 
     **/
    const UI::Editor::EditorScrollbarGeometry geometry = m_model.calculate_geometry(
        get_track_bounds(layout), 28.0F * layout.dpi_scale);
    const UI::Theme::Color thumb_color = m_model.is_dragging()
        ? UI::Theme::Color{255, 255, 255, 115}
        : (m_hovered ? UI::Theme::Color{255, 255, 255, 75} : UI::Theme::Color{255, 255, 255, 40});
    surface.fill_rectangle(
        device_context,
        geometry.thumb,
        thumb_color);
}

UI::Rect EditorScrollbar::get_track_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) noexcept
{
    return layout.scrollbar_bounds;
}

} // namespace Zenvra::Platform::Win32::Components
