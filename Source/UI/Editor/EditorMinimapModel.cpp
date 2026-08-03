#include "UI/Editor/EditorMinimapModel.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::UI::Editor
{

void EditorMinimapModel::synchronize(
    std::size_t total_lines,
    std::size_t visible_lines,
    std::size_t first_visible_line) noexcept
{
    m_total_lines = std::max<std::size_t>(total_lines, 1);
    m_visible_lines = std::clamp<std::size_t>(visible_lines, 1, m_total_lines);
    const std::size_t maximum_first_line = m_total_lines - m_visible_lines;
    m_first_visible_line = std::min(first_visible_line, maximum_first_line);
}

std::size_t EditorMinimapModel::calculate_sample_count(
    const Rect& bounds,
    float minimum_row_height) const noexcept
{
    if (bounds.is_empty())
    {
        return 0;
    }
    const float safe_row_height = std::max(minimum_row_height, 1.0F);
    const std::size_t available_rows = static_cast<std::size_t>(std::max(
        static_cast<int>(std::floor(bounds.height / safe_row_height)), 1));
    return std::min(m_total_lines, available_rows);
}

std::size_t EditorMinimapModel::get_line_for_sample(
    std::size_t sample_index,
    std::size_t sample_count) const noexcept
{
    if (sample_count <= 1 || m_total_lines <= 1)
    {
        return 0;
    }
    sample_index = std::min(sample_index, sample_count - 1);
    return sample_index * (m_total_lines - 1) / (sample_count - 1);
}

std::size_t EditorMinimapModel::get_first_visible_line_for_point(
    float point_y,
    const Rect& bounds) const noexcept
{
    if (bounds.is_empty() || m_total_lines <= m_visible_lines)
    {
        return 0;
    }
    const float ratio = std::clamp(
        (point_y - bounds.y) / bounds.height, 0.0F, 1.0F);
    const std::size_t centered_line = static_cast<std::size_t>(std::lround(
        ratio * static_cast<float>(m_total_lines - 1)));
    const std::size_t half_page = m_visible_lines / 2;
    const std::size_t requested_first = centered_line > half_page
        ? centered_line - half_page
        : 0;
    return std::min(requested_first, m_total_lines - m_visible_lines);
}

Rect EditorMinimapModel::calculate_viewport_bounds(
    const Rect& bounds,
    float minimum_viewport_height) const noexcept
{
    if (bounds.is_empty())
    {
        return bounds;
    }
    const float height = std::clamp(
        bounds.height * static_cast<float>(m_visible_lines) /
            static_cast<float>(m_total_lines),
        std::min(std::max(minimum_viewport_height, 1.0F), bounds.height),
        bounds.height);
    const std::size_t maximum_first_line = m_total_lines - m_visible_lines;
    const float ratio = maximum_first_line == 0
        ? 0.0F
        : static_cast<float>(m_first_visible_line) /
            static_cast<float>(maximum_first_line);
    return {
        bounds.x,
        bounds.y + (bounds.height - height) * ratio,
        bounds.width,
        height,
    };
}

} // namespace Zenvra::UI::Editor
