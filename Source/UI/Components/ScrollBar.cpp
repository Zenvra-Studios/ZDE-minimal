#include "UI/Components/ScrollBar.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::UI::Components
{

ScrollBar::ScrollBar(Rect track_bounds, ScrollBarOrientation orientation) noexcept
    : m_track_bounds{track_bounds}
    , m_orientation{orientation}
{
}

void ScrollBar::reset() noexcept
{
    m_total_items = 1;
    m_visible_items = 1;
    m_scroll_offset = 0;
    m_pointer_offset = 0.0F;
    m_state = ScrollBarState{};
}

bool ScrollBar::set_metrics(std::size_t total_items, std::size_t visible_items) noexcept
{
    m_total_items = std::max<std::size_t>(total_items, 1);
    m_visible_items = std::clamp<std::size_t>(visible_items, 1, m_total_items);
    return set_scroll_offset_internal(m_scroll_offset);
}

bool ScrollBar::scroll_by(std::ptrdiff_t delta) noexcept
{
    const std::ptrdiff_t maximum = static_cast<std::ptrdiff_t>(get_maximum_offset());
    const std::ptrdiff_t current = static_cast<std::ptrdiff_t>(m_scroll_offset);
    const std::ptrdiff_t next = std::clamp(current + delta, std::ptrdiff_t{0}, maximum);
    return set_scroll_offset_internal(static_cast<std::size_t>(next));
}

bool ScrollBar::scroll_to(std::size_t index) noexcept
{
    return set_scroll_offset_internal(index);
}

bool ScrollBar::reveal(std::size_t index) noexcept
{
    index = std::min(index, m_total_items > 0 ? m_total_items - 1 : 0);
    if (index < m_scroll_offset)
    {
        return set_scroll_offset_internal(index);
    }
    if (index >= m_scroll_offset + m_visible_items)
    {
        return set_scroll_offset_internal(index - m_visible_items + 1);
    }
    return false;
}

std::size_t ScrollBar::get_maximum_offset() const noexcept
{
    if (m_total_items > m_visible_items)
    {
        return m_total_items - m_visible_items;
    }
    return 0;
}

ScrollBarGeometry ScrollBar::calculate_geometry() const noexcept
{
    return calculate_geometry(m_track_bounds, m_min_thumb_size);
}

ScrollBarGeometry ScrollBar::calculate_geometry(const Rect& track, float min_thumb_size) const noexcept
{
    ScrollBarGeometry geometry{.track = track, .thumb = track};
    if (track.is_empty() || m_total_items == 0 || m_visible_items == 0)
    {
        return geometry;
    }

    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        const float effective_track_h = track.height;
        if (effective_track_h <= 0.0F)
        {
            return geometry;
        }

        const float ratio = std::min(static_cast<float>(m_visible_items) / static_cast<float>(m_total_items), 1.0F);
        const float thumb_h = std::clamp(
            effective_track_h * ratio,
            std::min(std::max(min_thumb_size, 1.0F), effective_track_h),
            effective_track_h);
        const std::size_t max_offset = get_maximum_offset();
        const float progress = max_offset == 0
            ? 0.0F
            : static_cast<float>(m_scroll_offset) / static_cast<float>(max_offset);
        const float max_travel = effective_track_h - thumb_h;

        geometry.thumb = Rect{
            track.x + m_thumb_inset,
            track.y + progress * max_travel,
            std::max(track.width - m_thumb_inset * 2.0F, 1.0F),
            thumb_h
        };
    }
    else
    {
        const float effective_track_w = track.width;
        if (effective_track_w <= 0.0F)
        {
            return geometry;
        }

        const float ratio = std::min(static_cast<float>(m_visible_items) / static_cast<float>(m_total_items), 1.0F);
        const float thumb_w = std::clamp(
            effective_track_w * ratio,
            std::min(std::max(min_thumb_size, 1.0F), effective_track_w),
            effective_track_w);
        const std::size_t max_offset = get_maximum_offset();
        const float progress = max_offset == 0
            ? 0.0F
            : static_cast<float>(m_scroll_offset) / static_cast<float>(max_offset);
        const float max_travel = effective_track_w - thumb_w;

        geometry.thumb = Rect{
            track.x + progress * max_travel,
            track.y + m_thumb_inset,
            thumb_w,
            std::max(track.height - m_thumb_inset * 2.0F, 1.0F)
        };
    }
    return geometry;
}

Rect ScrollBar::get_thumb_bounds() const noexcept
{
    return calculate_geometry().thumb;
}

bool ScrollBar::is_point_in_track(float point_x, float point_y) const noexcept
{
    return m_track_bounds.contains(point_x, point_y);
}

bool ScrollBar::is_point_in_thumb(float point_x, float point_y) const noexcept
{
    return get_thumb_bounds().contains(point_x, point_y);
}

bool ScrollBar::handle_pointer_move(float point_x, float point_y) noexcept
{
    const bool was_hovered = m_state.hovered;
    m_state.hovered = is_point_in_track(point_x, point_y) && is_needed();
    return was_hovered != m_state.hovered;
}

bool ScrollBar::handle_pointer_press(float point_x, float point_y) noexcept
{
    if (!is_point_in_track(point_x, point_y) || !is_needed())
    {
        return false;
    }

    const ScrollBarGeometry geo = calculate_geometry();
    const bool on_thumb = geo.thumb.contains(point_x, point_y);

    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        if (on_thumb)
        {
            m_pointer_offset = point_y - geo.thumb.y;
        }
        else
        {
            m_pointer_offset = geo.thumb.height * 0.5F;
            const float travel = std::max(m_track_bounds.height - geo.thumb.height, 0.0F);
            static_cast<void>(update_from_thumb_pos(point_y - m_pointer_offset, travel, m_track_bounds.y, geo.thumb.height));
        }
    }
    else
    {
        if (on_thumb)
        {
            m_pointer_offset = point_x - geo.thumb.x;
        }
        else
        {
            m_pointer_offset = geo.thumb.width * 0.5F;
            const float travel = std::max(m_track_bounds.width - geo.thumb.width, 0.0F);
            static_cast<void>(update_from_thumb_pos(point_x - m_pointer_offset, travel, m_track_bounds.x, geo.thumb.width));
        }
    }

    m_state.dragging = true;
    m_state.pressed = true;
    return true;
}

bool ScrollBar::handle_pointer_drag(float point_x, float point_y) noexcept
{
    if (!m_state.dragging)
    {
        return false;
    }

    const ScrollBarGeometry geo = calculate_geometry();
    if (m_orientation == ScrollBarOrientation::Vertical)
    {
        const float travel = std::max(m_track_bounds.height - geo.thumb.height, 0.0F);
        return update_from_thumb_pos(point_y - m_pointer_offset, travel, m_track_bounds.y, geo.thumb.height);
    }
    else
    {
        const float travel = std::max(m_track_bounds.width - geo.thumb.width, 0.0F);
        return update_from_thumb_pos(point_x - m_pointer_offset, travel, m_track_bounds.x, geo.thumb.width);
    }
}

bool ScrollBar::handle_pointer_release() noexcept
{
    const bool was_dragging = m_state.dragging;
    m_state.dragging = false;
    m_state.pressed = false;
    m_pointer_offset = 0.0F;
    return was_dragging;
}

bool ScrollBar::set_scroll_offset_internal(std::size_t offset) noexcept
{
    const std::size_t next = std::min(offset, get_maximum_offset());
    const bool changed = next != m_scroll_offset;
    m_scroll_offset = next;
    if (changed && m_on_scroll)
    {
        m_on_scroll(m_scroll_offset);
    }
    return changed;
}

bool ScrollBar::update_from_thumb_pos(
    float thumb_pos,
    float travel_length,
    float track_start,
    float thumb_size) noexcept
{
    (void)thumb_size;
    if (travel_length <= 0.0F)
    {
        return set_scroll_offset_internal(0);
    }
    const float clamped = std::clamp(thumb_pos, track_start, track_start + travel_length);
    const float ratio = (clamped - track_start) / travel_length;
    const std::size_t target = static_cast<std::size_t>(std::lround(
        ratio * static_cast<float>(get_maximum_offset())));
    return set_scroll_offset_internal(target);
}

} // namespace Zenvra::UI::Components
