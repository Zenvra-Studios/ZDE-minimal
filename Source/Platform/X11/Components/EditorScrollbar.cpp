#include "Platform/X11/Components/EditorScrollbar.h"

#include "Platform/X11/Components/StudioWorkspaceRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Zenvra::Platform::X11::Components
{

namespace
{

unsigned long long get_current_time_ms() noexcept
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

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

bool EditorScrollbar::tick_animation() noexcept
{
    return false;
}

float EditorScrollbar::get_animated_scroll_lines() const noexcept
{
    return static_cast<float>(m_model.get_first_visible_line());
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
    const bool was_dragging = m_model.is_dragging();
    m_model.end_pointer_drag();
    return was_dragging;
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
    Drawable drawable,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    surface.fill_rectangle(
        drawable, layout.scrollbar_bounds, surface.m_pixels.editor_background);
    if (m_model.get_maximum_first_line() == 0)
    {
        return;
    }
    const UI::Editor::EditorScrollbarGeometry geometry = m_model.calculate_geometry(
        get_track_bounds(layout), 28.0F * layout.dpi_scale);

    const UI::Theme::Color thumb_color = m_model.is_dragging()
        ? UI::Theme::Color{255, 255, 255, 115}
        : (m_hovered ? UI::Theme::Color{255, 255, 255, 75} : UI::Theme::Color{255, 255, 255, 40});

    const auto& bg = surface.m_palette.editor_background;
    const std::uint32_t a = thumb_color.alpha;
    const UI::Theme::Color blended_thumb{
        static_cast<std::uint8_t>((thumb_color.red * a + bg.red * (255 - a) + 127) / 255),
        static_cast<std::uint8_t>((thumb_color.green * a + bg.green * (255 - a) + 127) / 255),
        static_cast<std::uint8_t>((thumb_color.blue * a + bg.blue * (255 - a) + 127) / 255),
        255
    };

    surface.fill_rectangle(
        drawable,
        geometry.thumb,
        surface.allocate_color(blended_thumb));
}

UI::Rect EditorScrollbar::get_track_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) noexcept
{
    return layout.scrollbar_bounds;
}

} // namespace Zenvra::Platform::X11::Components
