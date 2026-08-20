#include "UI/Editor/EditorScrollModel.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::UI::Editor
{

bool EditorScrollModel::set_line_metrics(
    std::size_t total_lines,
    std::size_t visible_lines) noexcept
{
    m_total_lines = std::max<std::size_t>(total_lines, 1);
    m_visible_lines = std::clamp<std::size_t>(visible_lines, 1, m_total_lines);
    return set_first_visible_line(m_first_visible_line);
}

bool EditorScrollModel::scroll_lines(std::ptrdiff_t line_delta) noexcept
{
    const std::ptrdiff_t maximum = static_cast<std::ptrdiff_t>(get_maximum_first_line());
    const std::ptrdiff_t current = static_cast<std::ptrdiff_t>(m_first_visible_line);
    const std::ptrdiff_t next = std::clamp(current + line_delta, std::ptrdiff_t{0}, maximum);
    return set_first_visible_line(static_cast<std::size_t>(next));
}

bool EditorScrollModel::scroll_to(std::size_t first_visible_line) noexcept
{
    return set_first_visible_line(first_visible_line);
}

bool EditorScrollModel::reveal_line(std::size_t line_index) noexcept
{
    line_index = std::min(line_index, m_total_lines - 1);
    if (line_index < m_first_visible_line)
    {
        return set_first_visible_line(line_index);
    }
    if (line_index >= m_first_visible_line + m_visible_lines)
    {
        return set_first_visible_line(line_index - m_visible_lines + 1);
    }
    return false;
}

bool EditorScrollModel::begin_pointer_drag(
    float point_y,
    const Rect& track,
    float minimum_thumb_height) noexcept
{
    if (track.is_empty())
    {
        return false;
    }
    const EditorScrollbarGeometry geometry = calculate_geometry(track, minimum_thumb_height);
    if (geometry.thumb.contains(geometry.thumb.x + geometry.thumb.width * 0.5F, point_y))
    {
        m_pointer_offset = point_y - geometry.thumb.y;
        m_dragging = true;
        return false;
    }

    m_pointer_offset = geometry.thumb.height * 0.5F;
    m_dragging = true;
    return update_from_thumb_top(
        point_y - m_pointer_offset, track, geometry.thumb.height);
}

bool EditorScrollModel::drag_pointer(
    float point_y,
    const Rect& track,
    float minimum_thumb_height) noexcept
{
    if (!m_dragging)
    {
        return false;
    }
    const EditorScrollbarGeometry geometry = calculate_geometry(track, minimum_thumb_height);
    return update_from_thumb_top(
        point_y - m_pointer_offset, track, geometry.thumb.height);
}

bool EditorScrollModel::end_pointer_drag() noexcept
{
    const bool was_dragging = m_dragging;
    m_dragging = false;
    m_pointer_offset = 0.0F;
    return was_dragging;
}

std::size_t EditorScrollModel::get_first_visible_line() const noexcept
{
    return m_first_visible_line;
}

std::size_t EditorScrollModel::get_maximum_first_line() const noexcept
{
    if (m_total_lines <= 1)
    {
        return 0;
    }
    return m_total_lines - 1;
}

bool EditorScrollModel::is_dragging() const noexcept
{
    return m_dragging;
}

EditorScrollbarGeometry EditorScrollModel::calculate_geometry(
    const Rect& track,
    float minimum_thumb_height) const noexcept
{
    EditorScrollbarGeometry geometry{.track = track, .thumb = track};
    if (track.is_empty())
    {
        return geometry;
    }

    if (m_total_lines <= m_visible_lines)
    {
        geometry.thumb = track;
        return geometry;
    }

    const float visible_ratio = std::min(
        static_cast<float>(m_visible_lines) / static_cast<float>(m_total_lines), 1.0F);
    const float thumb_height = std::clamp(
        track.height * visible_ratio,
        std::min(std::max(minimum_thumb_height, 1.0F), track.height),
        track.height);
    const std::size_t maximum_first_line = get_maximum_first_line();
    const float scroll_ratio = maximum_first_line == 0
        ? 0.0F
        : std::clamp(static_cast<float>(m_first_visible_line) /
            static_cast<float>(maximum_first_line), 0.0F, 1.0F);
    geometry.thumb = {
        track.x,
        track.y + (track.height - thumb_height) * scroll_ratio,
        track.width,
        thumb_height,
    };
    return geometry;
}

bool EditorScrollModel::set_first_visible_line(std::size_t line_index) noexcept
{
    const std::size_t next = std::min(line_index, get_maximum_first_line());
    const bool changed = next != m_first_visible_line;
    m_first_visible_line = next;
    return changed;
}

bool EditorScrollModel::update_from_thumb_top(
    float thumb_top,
    const Rect& track,
    float thumb_height) noexcept
{
    const float travel = std::max(track.height - thumb_height, 0.0F);
    if (travel <= 0.0F)
    {
        return set_first_visible_line(0);
    }
    const float clamped_top = std::clamp(thumb_top, track.y, track.y + travel);
    const float ratio = (clamped_top - track.y) / travel;
    const std::size_t target = static_cast<std::size_t>(std::lround(
        ratio * static_cast<float>(get_maximum_first_line())));
    return set_first_visible_line(target);
}

} // namespace Zenvra::UI::Editor
