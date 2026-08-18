#pragma once

#include "UI/Geometry.h"

#include <algorithm>
#include <cstddef>
#include <functional>

namespace Zenvra::UI::Components
{

enum class ScrollBarOrientation
{
    Vertical,
    Horizontal
};

struct ScrollBarGeometry
{
    Rect track;
    Rect thumb;
};

struct ScrollBarState
{
    bool hovered = false;
    bool dragging = false;
    bool pressed = false;
};

class ScrollBar
{
public:
    ScrollBar() = default;
    explicit ScrollBar(
        Rect track_bounds,
        ScrollBarOrientation orientation = ScrollBarOrientation::Vertical) noexcept;

    void reset() noexcept;

    // Metrics configuration
    bool set_metrics(std::size_t total_items, std::size_t visible_items) noexcept;
    void set_track_bounds(const Rect& track) noexcept { m_track_bounds = track; }
    [[nodiscard]] const Rect& get_track_bounds() const noexcept { return m_track_bounds; }

    void set_orientation(ScrollBarOrientation orientation) noexcept { m_orientation = orientation; }
    [[nodiscard]] ScrollBarOrientation get_orientation() const noexcept { return m_orientation; }

    void set_minimum_thumb_size(float min_size) noexcept { m_min_thumb_size = min_size; }
    [[nodiscard]] float get_minimum_thumb_size() const noexcept { return m_min_thumb_size; }

    void set_thumb_inset(float inset) noexcept { m_thumb_inset = inset; }
    [[nodiscard]] float get_thumb_inset() const noexcept { return m_thumb_inset; }

    void set_corner_radius(float radius) noexcept { m_corner_radius = radius; }
    [[nodiscard]] float get_corner_radius() const noexcept { return m_corner_radius; }

    // Scroll operations
    [[nodiscard]] bool scroll_by(std::ptrdiff_t delta) noexcept;
    [[nodiscard]] bool scroll_to(std::size_t index) noexcept;
    [[nodiscard]] bool reveal(std::size_t index) noexcept;

    // State & queries
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept { return m_scroll_offset; }
    [[nodiscard]] std::size_t get_first_visible_item() const noexcept { return m_scroll_offset; }
    [[nodiscard]] std::size_t get_total_items() const noexcept { return m_total_items; }
    [[nodiscard]] std::size_t get_visible_items() const noexcept { return m_visible_items; }
    [[nodiscard]] std::size_t get_maximum_offset() const noexcept;
    [[nodiscard]] bool is_needed() const noexcept { return m_total_items > m_visible_items && m_visible_items > 0; }

    [[nodiscard]] const ScrollBarState& get_state() const noexcept { return m_state; }
    [[nodiscard]] bool is_hovered() const noexcept { return m_state.hovered; }
    [[nodiscard]] bool is_dragging() const noexcept { return m_state.dragging; }
    [[nodiscard]] bool is_pressed() const noexcept { return m_state.pressed; }
    void set_hovered(bool hovered) noexcept { m_state.hovered = hovered; }

    // Geometry calculation
    [[nodiscard]] ScrollBarGeometry calculate_geometry() const noexcept;
    [[nodiscard]] ScrollBarGeometry calculate_geometry(const Rect& track, float min_thumb_size) const noexcept;
    [[nodiscard]] Rect get_thumb_bounds() const noexcept;

    // Pointer events
    [[nodiscard]] bool is_point_in_track(float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_point_in_thumb(float point_x, float point_y) const noexcept;
    [[nodiscard]] bool handle_pointer_move(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_press(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_drag(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;

    void set_on_scroll(std::function<void(std::size_t)> callback) { m_on_scroll = std::move(callback); }

private:
    [[nodiscard]] bool set_scroll_offset_internal(std::size_t offset) noexcept;
    [[nodiscard]] bool update_from_thumb_pos(
        float thumb_pos,
        float travel_length,
        float track_start,
        float thumb_size) noexcept;

    Rect m_track_bounds;
    ScrollBarOrientation m_orientation = ScrollBarOrientation::Vertical;
    std::size_t m_total_items = 1;
    std::size_t m_visible_items = 1;
    std::size_t m_scroll_offset = 0;
    float m_min_thumb_size = 20.0F;
    float m_thumb_inset = 0.0F;
    float m_corner_radius = 0.0F;
    float m_pointer_offset = 0.0F;
    ScrollBarState m_state;
    std::function<void(std::size_t)> m_on_scroll;
};

using Scrollbar = ScrollBar;

} // namespace Zenvra::UI::Components
