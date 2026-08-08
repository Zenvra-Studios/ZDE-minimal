#pragma once

#include <span>
#include <string_view>

namespace Zenvra::UI::Components
{

struct MenuItem
{
    std::string_view label;
    std::string_view command_id;
    bool separator = false;
    std::string_view shortcut = {};
};

struct Menu
{
    std::string_view label;
    std::span<const MenuItem> items;
};

[[nodiscard]] std::span<const Menu> get_window_menus() noexcept;
[[nodiscard]] std::span<const MenuItem> get_compiler_menu() noexcept;
[[nodiscard]] std::span<const MenuItem> get_platform_menu() noexcept;
[[nodiscard]] std::span<const MenuItem> get_binary_menu() noexcept;
[[nodiscard]] std::span<const MenuItem> get_gear_menu() noexcept;
[[nodiscard]] std::span<const MenuItem> get_ellipsis_menu() noexcept;

} // namespace Zenvra::UI::Components
