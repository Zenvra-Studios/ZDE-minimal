#ifndef ZENVRA_UTILITY_UI_HOVER_H
#define ZENVRA_UTILITY_UI_HOVER_H

#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Theme/StudioTheme.h"
#include <optional>

namespace Zenvra::Utility::UiHover
{
    /**
     * @brief Helper function to determine the background color of a window control
     *        based on its hover and pressed state.
     * 
     * @param current_control The control being drawn.
     * @param hovered_control The control currently being hovered by the mouse.
     * @param pressed_control The control currently being pressed by the mouse.
     * @param theme The application theme containing color definitions.
     * @return The color to draw the background, or std::nullopt if no background should be drawn.
     */
    inline std::optional<UI::Theme::Color> get_control_background_color(
        UI::Chrome::WindowControl current_control,
        UI::Chrome::WindowControl hovered_control,
        UI::Chrome::WindowControl pressed_control,
        const UI::Theme::StudioTheme& theme)
    {
        if (current_control == UI::Chrome::WindowControl::NoControl)
        {
            return std::nullopt;
        }

        if (pressed_control == current_control)
        {
            return theme.pressed;
        }
        else if (hovered_control == current_control)
        {
            return current_control == UI::Chrome::WindowControl::Close ? theme.close_hover : theme.hover;
        }

        return std::nullopt;
    }
} // namespace Zenvra::Utility::UiHover

#endif // ZENVRA_UTILITY_UI_HOVER_H
