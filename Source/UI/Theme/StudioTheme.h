#pragma once

#include <cstdint>

namespace Zenvra::UI::Theme
{

struct Color
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    constexpr bool operator==(const Color&) const noexcept = default;
};

struct StudioTheme
{
    Color window_background;
    Color titlebar_background;
    Color titlebar_border;
    Color panel_background;
    Color text_primary;
    Color text_secondary;
    Color accent;
    Color hover;
    Color pressed;
    Color command_center_background;
    Color command_center_border;
    Color close_hover;

    [[nodiscard]] static StudioTheme zenvra_dark() noexcept;
};

} // namespace Zenvra::UI::Theme
