#pragma once

// X11 headers define `None` as a macro (0L) which collides with
// `BackdropEffect::None`. Guard here so this header parses correctly
// whether or not <X11/Xlib.h> was included first; restore afterwards
// so X11 code using `None` keeps working.
#ifdef None
#pragma push_macro("None")
#undef None
#define ZDE_PUSHED_NONE_MACRO
#endif

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

[[nodiscard]] constexpr Color dim_color(const Color& fg, const Color& bg, float opacity = 0.45F) noexcept
{
    const float inv = 1.0F - opacity;
    const auto r = static_cast<int>(static_cast<float>(fg.red) * opacity + static_cast<float>(bg.red) * inv + 0.5F);
    const auto g = static_cast<int>(static_cast<float>(fg.green) * opacity + static_cast<float>(bg.green) * inv + 0.5F);
    const auto b = static_cast<int>(static_cast<float>(fg.blue) * opacity + static_cast<float>(bg.blue) * inv + 0.5F);
    return Color{
        static_cast<std::uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r)),
        static_cast<std::uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g)),
        static_cast<std::uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b)),
        255
    };
}

enum class BackdropEffect : std::uint8_t
{
    None = 0,
    Mica,
    Acrylic,
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

    bool is_dark = true;
    bool is_modern = false;
    bool enable_os_blur = false;
    BackdropEffect backdrop_effect = BackdropEffect::None;

    [[nodiscard]] static StudioTheme zenvra_dark() noexcept;
    [[nodiscard]] static StudioTheme zenvra_light() noexcept;
    [[nodiscard]] static StudioTheme zenvra_dark_modern() noexcept;
    [[nodiscard]] static StudioTheme zenvra_light_modern() noexcept;
    [[nodiscard]] static StudioTheme high_contrast() noexcept;
};

} // namespace Zenvra::UI::Theme

#ifdef ZDE_PUSHED_NONE_MACRO
#pragma pop_macro("None")
#undef ZDE_PUSHED_NONE_MACRO
#endif
