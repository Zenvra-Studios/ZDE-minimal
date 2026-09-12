#include "Input.h"

namespace Zenvra::UI::Components
{

static bool is_utf8_continuation_byte(unsigned char c) noexcept
{
    return (c & 0xC0U) == 0x80U;
}

static std::size_t prev_utf8_char_offset(std::string_view text, std::size_t index) noexcept
{
    if (index == 0)
    {
        return 0;
    }
    std::size_t pos = index - 1;
    while (pos > 0 && is_utf8_continuation_byte(static_cast<unsigned char>(text[pos])))
    {
        pos--;
    }
    return pos;
}

static std::size_t next_utf8_char_offset(std::string_view text, std::size_t index) noexcept
{
    if (index >= text.length())
    {
        return text.length();
    }
    std::size_t pos = index + 1;
    while (pos < text.length() && is_utf8_continuation_byte(static_cast<unsigned char>(text[pos])))
    {
        pos++;
    }
    return pos;
}

Input::Input(std::string placeholder, UI::Rect bounds)
    : m_placeholder{std::move(placeholder)}
    , m_bounds{bounds}
{
}

void Input::set_text(std::string text)
{
    m_text = std::move(text);
    m_state.cursor_position = m_text.length();
    clear_selection();
    reset_blink();
}

void Input::set_focused(bool focused) noexcept
{
    m_state.focused = focused;
    if (focused)
    {
        reset_blink();
    }
    else
    {
        clear_selection();
    }
}

bool Input::tick() noexcept
{
    if (!m_state.focused)
    {
        return false;
    }
    return m_caret_blink.tick();
}

bool Input::is_caret_visible() const noexcept
{
    return m_state.focused && m_caret_blink.is_visible();
}

void Input::reset_blink() noexcept
{
    m_caret_blink.reset();
}

std::size_t Input::find_cursor_offset(
    float relative_x,
    const std::function<float(std::string_view)>& text_width_fn) const noexcept
{
    if (m_text.empty() || relative_x <= 0.0F)
    {
        return 0;
    }

    if (!text_width_fn)
    {
        return m_text.length();
    }

    std::size_t best_offset = 0;
    float best_diff = relative_x;
    std::size_t offset = 0;

    while (offset <= m_text.length())
    {
        const float w = text_width_fn(std::string_view{m_text.data(), offset});
        const float diff = std::abs(relative_x - w);
        if (diff < best_diff)
        {
            best_diff = diff;
            best_offset = offset;
        }
        if (offset >= m_text.length())
        {
            break;
        }
        offset = next_utf8_char_offset(m_text, offset);
    }

    return best_offset;
}

bool Input::handle_pointer_press(
    float point_x,
    float point_y,
    std::function<float(std::string_view)> text_width_fn) noexcept
{
    if (m_bounds.contains(point_x, point_y))
    {
        m_state.focused = true;
        reset_blink();

        const float pad_x = 8.0F;
        const float rel_x = point_x - (m_bounds.x + pad_x);
        m_state.cursor_position = find_cursor_offset(rel_x, text_width_fn);
        m_state.selection_start = m_state.cursor_position;
        m_state.selection_end = m_state.cursor_position;
        m_is_dragging = true;
        return true;
    }

    m_state.focused = false;
    m_is_dragging = false;
    clear_selection();
    return false;
}

bool Input::handle_pointer_drag(
    float point_x,
    float point_y,
    std::function<float(std::string_view)> text_width_fn) noexcept
{
    static_cast<void>(point_y);
    if (!m_is_dragging || !m_state.focused)
    {
        return false;
    }

    const float pad_x = 8.0F;
    const float rel_x = point_x - (m_bounds.x + pad_x);
    const std::size_t new_pos = find_cursor_offset(rel_x, text_width_fn);

    if (new_pos != m_state.cursor_position)
    {
        m_state.cursor_position = new_pos;
        m_state.selection_end = new_pos;
        reset_blink();
        return true;
    }
    return false;
}

bool Input::handle_pointer_release() noexcept
{
    if (m_is_dragging)
    {
        m_is_dragging = false;
        return true;
    }
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
    if (!m_state.focused || text.empty())
    {
        return false;
    }

    if (has_selection())
    {
        delete_selection();
    }

    if (m_state.cursor_position > m_text.length())
    {
        m_state.cursor_position = m_text.length();
    }

    m_text.insert(m_state.cursor_position, text);
    m_state.cursor_position += text.length();
    clear_selection();
    reset_blink();

    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

bool Input::handle_char(char32_t codepoint) noexcept
{
    if (!m_state.focused || codepoint < 32 || codepoint == 127)
    {
        return false;
    }

    std::string utf8_char;
    if (codepoint <= 0x7F)
    {
        utf8_char += static_cast<char>(codepoint);
    }
    else if (codepoint <= 0x7FF)
    {
        utf8_char += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
        utf8_char += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else if (codepoint <= 0xFFFF)
    {
        utf8_char += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
        utf8_char += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    else if (codepoint <= 0x10FFFF)
    {
        utf8_char += static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07));
        utf8_char += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        utf8_char += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return handle_text_input(utf8_char);
}

bool Input::handle_backspace() noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    if (has_selection())
    {
        delete_selection();
        return true;
    }

    if (m_state.cursor_position == 0 || m_text.empty())
    {
        return false;
    }

    if (m_state.cursor_position > m_text.length())
    {
        m_state.cursor_position = m_text.length();
    }

    const std::size_t prev_pos = prev_utf8_char_offset(m_text, m_state.cursor_position);
    const std::size_t erase_len = m_state.cursor_position - prev_pos;
    m_text.erase(prev_pos, erase_len);
    m_state.cursor_position = prev_pos;
    clear_selection();
    reset_blink();

    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

bool Input::handle_delete() noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    if (has_selection())
    {
        delete_selection();
        return true;
    }

    if (m_state.cursor_position >= m_text.length() || m_text.empty())
    {
        return false;
    }

    const std::size_t next_pos = next_utf8_char_offset(m_text, m_state.cursor_position);
    const std::size_t erase_len = next_pos - m_state.cursor_position;
    m_text.erase(m_state.cursor_position, erase_len);
    clear_selection();
    reset_blink();

    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
    return true;
}

bool Input::handle_left(bool select) noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    if (!select && has_selection())
    {
        m_state.cursor_position = std::min(m_state.selection_start, m_state.selection_end);
        clear_selection();
        reset_blink();
        return true;
    }

    if (m_state.cursor_position == 0)
    {
        if (!select)
        {
            clear_selection();
        }
        return false;
    }

    const std::size_t prev_pos = prev_utf8_char_offset(m_text, m_state.cursor_position);
    m_state.cursor_position = prev_pos;

    if (select)
    {
        m_state.selection_end = m_state.cursor_position;
    }
    else
    {
        clear_selection();
    }
    reset_blink();
    return true;
}

bool Input::handle_right(bool select) noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    if (!select && has_selection())
    {
        m_state.cursor_position = std::max(m_state.selection_start, m_state.selection_end);
        clear_selection();
        reset_blink();
        return true;
    }

    if (m_state.cursor_position >= m_text.length())
    {
        if (!select)
        {
            clear_selection();
        }
        return false;
    }

    const std::size_t next_pos = next_utf8_char_offset(m_text, m_state.cursor_position);
    m_state.cursor_position = next_pos;

    if (select)
    {
        m_state.selection_end = m_state.cursor_position;
    }
    else
    {
        clear_selection();
    }
    reset_blink();
    return true;
}

bool Input::handle_home(bool select) noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    m_state.cursor_position = 0;
    if (select)
    {
        m_state.selection_end = 0;
    }
    else
    {
        clear_selection();
    }
    reset_blink();
    return true;
}

bool Input::handle_end(bool select) noexcept
{
    if (!m_state.focused)
    {
        return false;
    }

    m_state.cursor_position = m_text.length();
    if (select)
    {
        m_state.selection_end = m_text.length();
    }
    else
    {
        clear_selection();
    }
    reset_blink();
    return true;
}

void Input::select_all() noexcept
{
    m_state.selection_start = 0;
    m_state.selection_end = m_text.length();
    m_state.cursor_position = m_text.length();
    reset_blink();
}

void Input::clear_selection() noexcept
{
    m_state.selection_start = m_state.cursor_position;
    m_state.selection_end = m_state.cursor_position;
}

bool Input::has_selection() const noexcept
{
    return m_state.selection_start != m_state.selection_end;
}

std::string Input::get_selected_text() const
{
    if (!has_selection())
    {
        return "";
    }
    const std::size_t start = std::min(m_state.selection_start, m_state.selection_end);
    const std::size_t end = std::max(m_state.selection_start, m_state.selection_end);
    if (start >= m_text.length())
    {
        return "";
    }
    return m_text.substr(start, std::min(end - start, m_text.length() - start));
}

void Input::delete_selection()
{
    if (!has_selection())
    {
        return;
    }
    const std::size_t start = std::min(m_state.selection_start, m_state.selection_end);
    const std::size_t end = std::max(m_state.selection_start, m_state.selection_end);
    if (start < m_text.length())
    {
        m_text.erase(start, end - start);
    }
    m_state.cursor_position = start;
    clear_selection();
    reset_blink();
    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
}

void Input::clear() noexcept
{
    m_text.clear();
    m_state.cursor_position = 0;
    clear_selection();
    reset_blink();
    if (m_on_text_changed)
    {
        m_on_text_changed(m_text);
    }
}

std::string_view Input::get_text_before_cursor() const noexcept
{
    const std::size_t pos = std::min(m_state.cursor_position, m_text.length());
    return std::string_view{m_text.data(), pos};
}

std::string_view Input::get_text_after_cursor() const noexcept
{
    const std::size_t pos = std::min(m_state.cursor_position, m_text.length());
    return std::string_view{m_text.data() + pos, m_text.length() - pos};
}

void Input::set_cursor_position(std::size_t pos) noexcept
{
    m_state.cursor_position = std::min(pos, m_text.length());
    clear_selection();
    reset_blink();
}

} // namespace Zenvra::UI::Components
