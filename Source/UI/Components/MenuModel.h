#pragma once

#include "UI/Toolbar/ToolbarTypes.h"

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

void set_dynamic_binary_targets(
    std::span<const Toolbar::BinaryTargetProfile> targets,
    std::string_view toolchain_name = {});

} // namespace Zenvra::UI::Components
