#include "Button.h"

namespace Zenvra::UI::Components
{

Button::Button(std::string label, UI::Rect bounds)
    : m_label{std::move(label)}
    , m_bounds{bounds}
{
}

bool Button::handle_pointer_press(float point_x, float point_y) noexcept
{
    if (m_bounds.contains(point_x, point_y))
    {
        m_state.pressed = true;
        return true;
    }
    return false;
}

bool Button::handle_pointer_move(float point_x, float point_y) noexcept
{
    const bool was_hovered = m_state.hovered;
    m_state.hovered = m_bounds.contains(point_x, point_y);
    return was_hovered != m_state.hovered;
}

bool Button::handle_pointer_release(float point_x, float point_y) noexcept
{
    if (m_state.pressed)
    {
        m_state.pressed = false;
        if (m_bounds.contains(point_x, point_y) && m_on_click)
        {
            m_on_click();
            return true;
        }
    }
    return false;
}

} // namespace Zenvra::UI::Components
