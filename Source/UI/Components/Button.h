#pragma once

#include "UI/Geometry.h"
#include <string>
#include <functional>
#include <string_view>
#include <optional>

namespace Zenvra::UI::Components
{

struct ButtonState
{
    bool hovered = false;
    bool pressed = false;
};

class Button
{
public:
    Button() = default;
    Button(std::string label, UI::Rect bounds);

    [[nodiscard]] const std::string& get_label() const noexcept { return m_label; }
    void set_label(std::string label) { m_label = std::move(label); }

    [[nodiscard]] const std::optional<std::string>& get_icon() const noexcept { return m_icon; }
    void set_icon(std::string icon) { m_icon = std::move(icon); }

    [[nodiscard]] const UI::Rect& get_bounds() const noexcept { return m_bounds; }
    void set_bounds(const UI::Rect& bounds) { m_bounds = bounds; }

    [[nodiscard]] const ButtonState& get_state() const noexcept { return m_state; }
    void set_hovered(bool hovered) noexcept { m_state.hovered = hovered; }
    void set_pressed(bool pressed) noexcept { m_state.pressed = pressed; }

    [[nodiscard]] bool handle_pointer_press(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_move(float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release(float point_x, float point_y) noexcept;

    void set_on_click(std::function<void()> on_click) { m_on_click = std::move(on_click); }

private:
    std::string m_label;
    std::optional<std::string> m_icon;
    UI::Rect m_bounds;
    ButtonState m_state;
    std::function<void()> m_on_click;
};

} // namespace Zenvra::UI::Components
