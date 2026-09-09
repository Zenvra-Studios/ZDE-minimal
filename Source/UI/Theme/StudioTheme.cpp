#include "UI/Theme/StudioTheme.h"

namespace Zenvra::UI::Theme
{

StudioTheme StudioTheme::zenvra_dark() noexcept
{
    return StudioTheme{
        .window_background = {30, 31, 34, 255},
        // Keep the chrome base identical to the editor buffer strip so the
        // integrated file tabs read as one continuous titlebar surface.
        .titlebar_background = {29, 30, 33, 255},
        .titlebar_border = {43, 43, 43, 255},
        .panel_background = {28, 29, 32, 255},
        .text_primary = {240, 242, 248, 255},
        .text_secondary = {165, 170, 180, 255},
        .accent = {53, 132, 228, 255},
        .hover = {58, 62, 70, 255},
        .pressed = {68, 72, 82, 255},
        .command_center_background = {36, 37, 42, 255},
        .command_center_border = {43, 43, 43, 255},
        .close_hover = {196, 43, 28, 255},
        .is_dark = true,
        .enable_os_blur = false,
        .backdrop_effect = BackdropEffect::None,
    };
}

StudioTheme StudioTheme::zenvra_light() noexcept
{
    return StudioTheme{
        .window_background = {255, 255, 255, 255},
        .titlebar_background = {245, 245, 247, 255},
        .titlebar_border = {228, 228, 231, 255},
        .panel_background = {248, 249, 250, 255},
        .text_primary = {24, 24, 27, 255},
        .text_secondary = {107, 114, 128, 255},
        .accent = {0, 102, 204, 255},
        .hover = {235, 236, 240, 255},
        .pressed = {222, 225, 230, 255},
        .command_center_background = {238, 240, 243, 255},
        .command_center_border = {218, 220, 224, 255},
        .close_hover = {232, 17, 35, 255},
        .is_dark = false,
        .enable_os_blur = false,
        .backdrop_effect = BackdropEffect::None,
    };
}

StudioTheme StudioTheme::zenvra_dark_modern() noexcept
{
    auto theme = zenvra_dark();
    theme.is_modern = true;
    theme.enable_os_blur = true;
    theme.backdrop_effect = BackdropEffect::Acrylic;
    // Deep dark frosted glass OS blur for titlebar & activity bar; remaining window is solid
    theme.window_background = {24, 25, 28, 255};
    theme.titlebar_background = {12, 13, 16, 175};
    theme.titlebar_border = {50, 52, 60, 255};
    theme.panel_background = {22, 23, 26, 255};
    theme.command_center_background = {30, 31, 35, 255};
    theme.command_center_border = {65, 68, 75, 255};
    return theme;
}

StudioTheme StudioTheme::zenvra_light_modern() noexcept
{
    auto theme = zenvra_light();
    theme.is_modern = true;
    theme.enable_os_blur = true;
    theme.backdrop_effect = BackdropEffect::Acrylic;
    // Frosted glass OS blur for titlebar & activity bar; remaining window is solid
    theme.window_background = {255, 255, 255, 255};
    theme.titlebar_background = {245, 245, 250, 130};
    theme.titlebar_border = {220, 222, 226, 255};
    theme.panel_background = {248, 249, 250, 255};
    theme.command_center_background = {235, 237, 240, 255};
    theme.command_center_border = {218, 220, 224, 255};
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
