#pragma once

#include "UI/Geometry.h"
#include <string>
#include <string_view>
#include <functional>

namespace Zenvra::UI::Components
{

struct InputState
{
    bool focused = false;
    bool hovered = false;
    std::size_t cursor_position = 0;
};

class Input
{
public:
    Input() = default;
    Input(std::string placeholder, UI::Rect bounds);

    [[nodiscard]] const std::string& get_text() const noexcept { return m_text; }
    void set_text(std::string text) { m_text = std::move(text); m_state.cursor_position = m_text.length(); }

    [[nodiscard]] const std::string& get_placeholder() const noexcept { return m_placeholder; }
    void set_placeholder(std::string placeholder) { m_placeholder = std::move(placeholder); }

    [[nodiscard]] const UI::Rect& get_bounds() const noexcept { return m_bounds; }
    void set_bounds(const UI::Rect& bounds) { m_bounds = bounds; }

    [[nodiscard]] const InputState& get_state() const noexcept { return m_state; }
    void set_focused(bool focused) noexcept { m_state.focused = focused; }

    [[nodiscard]] bool handle_pointer_press(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_move(float point_x, float point_y) noexcept;
    
    [[nodiscard]] bool handle_text_input(std::string_view text);
    [[nodiscard]] bool handle_backspace();

    void set_on_text_changed(std::function<void(const std::string&)> on_text_changed) { m_on_text_changed = std::move(on_text_changed); }

private:
    std::string m_text;
    std::string m_placeholder;
    UI::Rect m_bounds;
    InputState m_state;
    std::function<void(const std::string&)> m_on_text_changed;
};

} // namespace Zenvra::UI::Components
