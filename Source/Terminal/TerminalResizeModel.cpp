#include "Terminal/TerminalResizeModel.h"

#include <algorithm>
#include <cmath>

namespace Zenvra::Terminal
{

bool TerminalResizeModel::set_hovered(bool hovered) noexcept
{
    const bool changed = m_hovered != hovered;
    m_hovered = hovered;
    return changed;
}

bool TerminalResizeModel::begin_resize() noexcept
{
    const bool changed = !m_resizing || m_maximized;
    if (m_maximized)
    {
        m_height = m_restore_height;
        m_maximized = false;
    }
    m_resizing = true;
    m_hovered = true;
    return changed;
}

bool TerminalResizeModel::resize_from_pointer(
    float point_y,
    float editor_top,
    float status_top,
    float dpi_scale) noexcept
{
    if (!m_resizing)
    {
        return false;
    }
    const float scale = std::max(dpi_scale, 0.5F);
    const float available_height = std::max((status_top - editor_top) / scale, 0.0F);
    const float min_terminal_height = 36.0F;
    const float requested_height = (status_top - point_y) / scale;
    const float next_height = std::clamp(requested_height, min_terminal_height, available_height);
    const bool changed = std::abs(next_height - m_height) > 0.1F;
    m_height = next_height;
    m_restore_height = next_height;
    return changed;
}

bool TerminalResizeModel::end_resize() noexcept
{
    const bool changed = m_resizing;
    m_resizing = false;
    return changed;
}

bool TerminalResizeModel::toggle_maximized() noexcept
{
    if (m_maximized)
    {
        m_maximized = false;
        m_height = m_restore_height;
    }
    else
    {
        m_restore_height = m_height;
        m_maximized = true;
    }
    m_resizing = false;
    m_hovered = true;
    return true;
}

void TerminalResizeModel::reset() noexcept
{
    m_height = default_height;
    m_restore_height = default_height;
    m_hovered = false;
    m_resizing = false;
    m_maximized = false;
}

float TerminalResizeModel::get_height() const noexcept { return m_height; }
bool TerminalResizeModel::is_hovered() const noexcept { return m_hovered; }
bool TerminalResizeModel::is_resizing() const noexcept { return m_resizing; }
bool TerminalResizeModel::is_maximized() const noexcept { return m_maximized; }

} // namespace Zenvra::Terminal
