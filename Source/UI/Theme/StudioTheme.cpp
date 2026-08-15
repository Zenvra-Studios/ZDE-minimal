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
        .titlebar_border = {48, 50, 55, 255},
        .panel_background = {28, 29, 32, 255},
        .text_primary = {188, 190, 196, 255},
        .text_secondary = {104, 107, 115, 255},
        .accent = {53, 132, 228, 255},
        .hover = {45, 47, 52, 255},
        .pressed = {53, 53, 56, 255},
        .command_center_background = {36, 37, 42, 255},
        .command_center_border = {48, 50, 55, 255},
        .close_hover = {196, 43, 28, 255},
    };
}

} // namespace Zenvra::UI::Theme
