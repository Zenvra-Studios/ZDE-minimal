#include "DragDropModel.h"

namespace Zenvra::Utility
{

void DragDropModel::begin_drag(std::size_t index, float start_position) noexcept
{
    m_dragging = true;
    m_index = index;
    m_target_index.reset();
    m_start_position = start_position;
    m_current_position = start_position;
}

bool DragDropModel::drag(float current_position) noexcept
{
    if (!m_dragging)
    {
        return false;
    }
    const bool changed = m_current_position != current_position;
    m_current_position = current_position;
    return changed;
}

std::optional<std::size_t> DragDropModel::end_drag() noexcept
{
    if (!m_dragging)
    {
        return std::nullopt;
    }
    m_dragging = false;
    std::optional<std::size_t> result = m_target_index;
    m_target_index.reset();
    return result;
}

bool DragDropModel::is_dragging() const noexcept
{
    return m_dragging;
}

std::size_t DragDropModel::get_dragged_index() const noexcept
{
    return m_index;
}

void DragDropModel::update_dragged_index(std::size_t new_index) noexcept
{
    m_index = new_index;
}

std::optional<std::size_t> DragDropModel::get_target_index() const noexcept
{
    return m_target_index;
}

void DragDropModel::set_target_index(std::optional<std::size_t> target) noexcept
{
    m_target_index = target;
}

float DragDropModel::get_drag_offset() const noexcept
{
    if (!m_dragging)
    {
        return 0.0f;
    }
    return m_current_position - m_start_position;
}

float DragDropModel::get_current_position() const noexcept
{
    return m_current_position;
}

} // namespace Zenvra::Utility
