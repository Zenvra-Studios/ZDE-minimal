#include "Input.h"

namespace Zenvra::UI::Components
{

Input::Input(std::string placeholder, UI::Rect bounds)
    : m_placeholder{std::move(placeholder)}
    , m_bounds{bounds}
{
}

bool Input::handle_pointer_press(float point_x, float point_y) noexcept
{
    if (m_bounds.contains(point_x, point_y))
    {
        m_state.focused = true;
        // In a real implementation we might calculate cursor pos based on x
        m_state.cursor_position = m_text.length();
        return true;
    }
    m_state.focused = false;
    return false;
}

bool Input::handle_pointer_move(float point_x, float point_y) noexcept
{
    const bool was_hovered = m_state.hovered;
    m_state.hovered = m_bounds.contains(point_x, point_y);
    return was_hovered != m_state.hovered;
}

bool Input::handle_text_input(std::string_view text)
{
    if (!m_state.focused)
    {
        return false;
    }
    m_text.insert(m_state.cursor_position, text);
    m_state.cursor_position += text.length();
    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

bool Input::handle_backspace()
{
    if (!m_state.focused || m_state.cursor_position == 0)
    {
        return false;
    }
    
    // Simplistic backspace assuming ASCII or 1-byte chars for now
    m_text.erase(m_state.cursor_position - 1, 1);
    m_state.cursor_position--;
    
    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

} // namespace Zenvra::UI::Components
