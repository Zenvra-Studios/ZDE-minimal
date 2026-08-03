#pragma once

#include "UI/Geometry.h"

#include <cstddef>

namespace Zenvra::UI::Editor
{

class EditorMinimapModel
{
public:
    void synchronize(
        std::size_t total_lines,
        std::size_t visible_lines,
        std::size_t first_visible_line) noexcept;

    [[nodiscard]] std::size_t calculate_sample_count(
        const Rect& bounds,
        float minimum_row_height) const noexcept;
    [[nodiscard]] std::size_t get_line_for_sample(
        std::size_t sample_index,
        std::size_t sample_count) const noexcept;
    [[nodiscard]] std::size_t get_first_visible_line_for_point(
        float point_y,
        const Rect& bounds) const noexcept;
    [[nodiscard]] Rect calculate_viewport_bounds(
        const Rect& bounds,
        float minimum_viewport_height) const noexcept;

private:
    std::size_t m_total_lines = 1;
    std::size_t m_visible_lines = 1;
    std::size_t m_first_visible_line = 0;
};

} // namespace Zenvra::UI::Editor
