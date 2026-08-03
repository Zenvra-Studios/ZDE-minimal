#pragma once

#include <span>
#include <string_view>

namespace Zenvra::UI::Chrome
{

struct WindowMenuItem
{
    std::string_view label;
    std::string_view command_id;
    bool separator = false;
};

struct WindowMenu
{
    std::string_view label;
    std::span<const WindowMenuItem> items;
};

[[nodiscard]] std::span<const WindowMenu> get_window_menu_model() noexcept;

} // namespace Zenvra::UI::Chrome
