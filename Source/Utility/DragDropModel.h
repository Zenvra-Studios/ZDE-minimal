#pragma once

#include <cstddef>
#include <optional>

namespace Zenvra::Utility
{

class DragDropModel
{
public:
    void begin_drag(std::size_t index, float start_position) noexcept;
    bool drag(float current_position) noexcept;
    std::optional<std::size_t> end_drag() noexcept;

    [[nodiscard]] bool is_dragging() const noexcept;
    [[nodiscard]] std::size_t get_dragged_index() const noexcept;
    void update_dragged_index(std::size_t new_index) noexcept;
    [[nodiscard]] std::optional<std::size_t> get_target_index() const noexcept;
    void set_target_index(std::optional<std::size_t> target) noexcept;
    [[nodiscard]] float get_drag_offset() const noexcept;
    [[nodiscard]] float get_current_position() const noexcept;

private:
    bool m_dragging = false;
    std::size_t m_index = 0;
    std::optional<std::size_t> m_target_index;
    float m_start_position = 0.0f;
    float m_current_position = 0.0f;
};

} // namespace Zenvra::Utility
