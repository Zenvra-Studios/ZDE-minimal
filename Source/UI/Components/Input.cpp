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
    if (!m_state.focused || m_state.cursor_position == 0 || m_text.empty())
    {
        return false;
    }
    
    std::size_t erase_len = 1;
    while (m_state.cursor_position >= erase_len + 1 &&
           (static_cast<unsigned char>(m_text[m_state.cursor_position - erase_len]) & 0xC0U) == 0x80U)
    {
        erase_len++;
    }
    m_text.erase(m_state.cursor_position - erase_len, erase_len);
    m_state.cursor_position -= erase_len;
    
    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

} // namespace Zenvra::UI::Components
