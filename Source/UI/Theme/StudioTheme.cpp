#include "UI/Theme/StudioTheme.h"

namespace Zenvra::UI::Theme
{

StudioTheme StudioTheme::zenvra_dark() noexcept
{
    return StudioTheme{
        .window_background = {30, 31, 35, 255},
        // Keep the chrome base consistent with the editor buffer strip so the
        // integrated file tabs read as one continuous titlebar surface.
        .titlebar_background = {27, 28, 31, 255},
        .titlebar_border = {45, 47, 54, 255},
        .panel_background = {27, 28, 31, 255},
        .text_primary = {222, 226, 234, 255},
        .text_secondary = {145, 152, 165, 255},
        .accent = {68, 140, 235, 255},
        .hover = {46, 49, 57, 255},
        .pressed = {58, 62, 72, 255},
        .command_center_background = {34, 36, 41, 255},
        .command_center_border = {50, 53, 62, 255},
        .close_hover = {196, 43, 28, 255},
        .is_dark = true,
        .is_modern = false,
        .enable_os_blur = false,
        .backdrop_effect = BackdropEffect::None,
    };
}

StudioTheme StudioTheme::zenvra_light() noexcept
{
    return StudioTheme{
        .window_background = {255, 255, 255, 255},
        .titlebar_background = {245, 245, 247, 255},
        .titlebar_border = {218, 222, 228, 255},
        .panel_background = {248, 249, 250, 255},
        .text_primary = {15, 15, 15, 255},
        .text_secondary = {50, 54, 62, 255},
        .accent = {0, 102, 204, 255},
        .hover = {235, 238, 242, 255},
        .pressed = {222, 226, 232, 255},
        .command_center_background = {238, 240, 243, 255},
        .command_center_border = {218, 222, 228, 255},
        .close_hover = {232, 17, 35, 255},
        .is_dark = false,
        .is_modern = false,
        .enable_os_blur = false,
        .backdrop_effect = BackdropEffect::None,
    };
}

StudioTheme StudioTheme::zenvra_dark_modern() noexcept
{
    auto theme = zenvra_dark();
    theme.is_modern = true;
    theme.enable_os_blur = false;
    theme.backdrop_effect = BackdropEffect::None;
    // Comfortable dark charcoal canvas for solid modern floating cards (not pitch-black)
    theme.window_background = {24, 25, 29, 255};
    theme.titlebar_background = theme.window_background;
    theme.titlebar_border = {45, 48, 56, 255};
    theme.panel_background = {32, 34, 40, 255};
    theme.command_center_background = {32, 34, 40, 255};
    theme.command_center_border = {48, 52, 62, 255};
    theme.hover = {48, 52, 62, 255};
    theme.pressed = {58, 62, 74, 255};
    return theme;
}

StudioTheme StudioTheme::zenvra_light_modern() noexcept
{
    auto theme = zenvra_light();
    theme.is_modern = true;
    theme.enable_os_blur = false;
    theme.backdrop_effect = BackdropEffect::None;
    // Soft neutral canvas allowing elevated white floating cards to stand out crisply
    theme.window_background = {243, 245, 248, 255};
    theme.titlebar_background = theme.window_background;
    theme.titlebar_border = {218, 222, 228, 255};
    theme.panel_background = {255, 255, 255, 255};
    theme.command_center_background = {255, 255, 255, 255};
    theme.command_center_border = {218, 222, 228, 255};
    theme.hover = {230, 234, 240, 255};
    theme.pressed = {220, 224, 232, 255};
    return theme;
}

StudioTheme StudioTheme::high_contrast() noexcept
{
    return StudioTheme{
        .window_background = {0, 0, 0, 255},
        .titlebar_background = {0, 0, 0, 255},
        .titlebar_border = {108, 142, 191, 255},
        .panel_background = {0, 0, 0, 255},
        .text_primary = {255, 255, 255, 255},
        .text_secondary = {255, 255, 255, 255},
        .accent = {243, 133, 24, 255},
        .hover = {30, 30, 30, 255},
        .pressed = {60, 60, 60, 255},
        .command_center_background = {0, 0, 0, 255},
        .command_center_border = {255, 255, 255, 255},
        .close_hover = {196, 43, 28, 255},
        .is_dark = true,
        .enable_os_blur = false,
        .backdrop_effect = BackdropEffect::None,
    };
}

} // namespace Zenvra::UI::Theme
