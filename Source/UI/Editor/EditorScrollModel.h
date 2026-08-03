#pragma once

#include "UI/Geometry.h"

#include <cstddef>

namespace Zenvra::UI::Editor
{

struct EditorScrollbarGeometry
{
    Rect track;
    Rect thumb;
};

class EditorScrollModel
{
public:
    [[nodiscard]] bool set_line_metrics(
        std::size_t total_lines,
        std::size_t visible_lines) noexcept;
    [[nodiscard]] bool scroll_lines(std::ptrdiff_t line_delta) noexcept;
    [[nodiscard]] bool scroll_to(std::size_t first_visible_line) noexcept;
    [[nodiscard]] bool reveal_line(std::size_t line_index) noexcept;
    [[nodiscard]] bool begin_pointer_drag(
        float point_y,
        const Rect& track,
        float minimum_thumb_height) noexcept;
    [[nodiscard]] bool drag_pointer(
        float point_y,
        const Rect& track,
        float minimum_thumb_height) noexcept;
    [[nodiscard]] bool end_pointer_drag() noexcept;

    [[nodiscard]] std::size_t get_first_visible_line() const noexcept;
    [[nodiscard]] std::size_t get_maximum_first_line() const noexcept;
    [[nodiscard]] bool is_dragging() const noexcept;
    [[nodiscard]] EditorScrollbarGeometry calculate_geometry(
        const Rect& track,
        float minimum_thumb_height) const noexcept;

private:
    [[nodiscard]] bool set_first_visible_line(std::size_t line_index) noexcept;
    [[nodiscard]] bool update_from_thumb_top(
        float thumb_top,
        const Rect& track,
        float thumb_height) noexcept;

    std::size_t m_total_lines = 1;
    std::size_t m_visible_lines = 1;
    std::size_t m_first_visible_line = 0;
    float m_pointer_offset = 0.0F;
    bool m_dragging = false;
};

} // namespace Zenvra::UI::Editor
