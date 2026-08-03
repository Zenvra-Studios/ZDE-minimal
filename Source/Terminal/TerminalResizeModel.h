#pragma once

namespace Zenvra::Terminal
{

class TerminalResizeModel
{
public:
    [[nodiscard]] bool set_hovered(bool hovered) noexcept;
    [[nodiscard]] bool begin_resize() noexcept;
    [[nodiscard]] bool resize_from_pointer(
        float point_y,
        float editor_top,
        float status_top,
        float dpi_scale) noexcept;
    [[nodiscard]] bool end_resize() noexcept;
    [[nodiscard]] bool toggle_maximized() noexcept;
    void reset() noexcept;

    [[nodiscard]] float get_height() const noexcept;
    [[nodiscard]] bool is_hovered() const noexcept;
    [[nodiscard]] bool is_resizing() const noexcept;
    [[nodiscard]] bool is_maximized() const noexcept;

private:
    static constexpr float default_height = 218.0F;

    float m_height = default_height;
    float m_restore_height = default_height;
    bool m_hovered = false;
    bool m_resizing = false;
    bool m_maximized = false;
};

} // namespace Zenvra::Terminal
