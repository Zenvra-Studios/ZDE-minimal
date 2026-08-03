#include "UI/Theme/StudioTheme.h"

namespace Zenvra::UI::Theme
{

StudioTheme StudioTheme::zenvra_dark() noexcept
{
    return StudioTheme{
        .window_background = {31, 31, 31, 255},
        .titlebar_background = {24, 24, 24, 255},
        .titlebar_border = {43, 43, 43, 255},
        .panel_background = {37, 37, 38, 255},
        .text_primary = {204, 204, 204, 255},
        .text_secondary = {154, 154, 154, 255},
        .accent = {0, 122, 204, 255},
        .hover = {45, 45, 45, 255},
        .pressed = {55, 55, 55, 255},
        .command_center_background = {36, 36, 36, 255},
        .command_center_border = {60, 60, 60, 255},
        .close_hover = {196, 43, 28, 255},
    };
}

} // namespace Zenvra::UI::Theme
