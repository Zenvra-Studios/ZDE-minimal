#pragma once

#include "UI/Editor/CaretBlinkModel.h"
#include "UI/Geometry.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace Zenvra::UI::Components
{

struct InputState
{
    bool focused = false;
    bool hovered = false;
    std::size_t cursor_position = 0;
    std::size_t selection_start = 0;
    std::size_t selection_end = 0;
};

class Input
{
public:
    Input() = default;
    Input(std::string placeholder, UI::Rect bounds);

    [[nodiscard]] const std::string& get_text() const noexcept { return m_text; }
    void set_text(std::string text);

    [[nodiscard]] const std::string& get_placeholder() const noexcept { return m_placeholder; }
    void set_placeholder(std::string placeholder) { m_placeholder = std::move(placeholder); }

    [[nodiscard]] const UI::Rect& get_bounds() const noexcept { return m_bounds; }
    void set_bounds(const UI::Rect& bounds) { m_bounds = bounds; }

    [[nodiscard]] const InputState& get_state() const noexcept { return m_state; }
    void set_focused(bool focused) noexcept;
    void focus() noexcept { set_focused(true); }
    void blur() noexcept { set_focused(false); }
    [[nodiscard]] bool is_focused() const noexcept { return m_state.focused; }

    // Blinking Caret API
    [[nodiscard]] bool tick() noexcept;
    [[nodiscard]] bool is_caret_visible() const noexcept;
    void reset_blink() noexcept;

    // Pointer Interaction (trigger / positioning / selection)
    [[nodiscard]] bool handle_pointer_press(
        float point_x,
        float point_y,
        std::function<float(std::string_view)> text_width_fn = nullptr) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        float point_x,
        float point_y,
        std::function<float(std::string_view)> text_width_fn = nullptr) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool handle_pointer_move(float point_x, float point_y) noexcept;

    // Text & Key Input
    [[nodiscard]] bool handle_text_input(std::string_view text);
    [[nodiscard]] bool handle_char(char32_t codepoint) noexcept;
    [[nodiscard]] bool handle_backspace() noexcept;
    [[nodiscard]] bool handle_delete() noexcept;

    // Cursor Navigation
    [[nodiscard]] bool handle_left(bool select = false) noexcept;
    [[nodiscard]] bool handle_right(bool select = false) noexcept;
    [[nodiscard]] bool handle_home(bool select = false) noexcept;
    [[nodiscard]] bool handle_end(bool select = false) noexcept;

    // Selection & Manipulation
    void select_all() noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] std::string get_selected_text() const;
    void delete_selection();
    void clear() noexcept;

    // Cursor & Substring Helpers for rendering
    [[nodiscard]] std::string_view get_text_before_cursor() const noexcept;
    [[nodiscard]] std::string_view get_text_after_cursor() const noexcept;
    [[nodiscard]] std::size_t get_cursor_position() const noexcept { return m_state.cursor_position; }
    void set_cursor_position(std::size_t pos) noexcept;

    void set_on_text_changed(std::function<void(const std::string&)> on_text_changed) { m_on_text_changed = std::move(on_text_changed); }

private:
    [[nodiscard]] std::size_t find_cursor_offset(
        float relative_x,
        const std::function<float(std::string_view)>& text_width_fn) const noexcept;

    std::string m_text;
    std::string m_placeholder;
    UI::Rect m_bounds;
    InputState m_state;
    Editor::CaretBlinkModel m_caret_blink;
    bool m_is_dragging = false;
    std::function<void(const std::string&)> m_on_text_changed;
};

} // namespace Zenvra::UI::Components
