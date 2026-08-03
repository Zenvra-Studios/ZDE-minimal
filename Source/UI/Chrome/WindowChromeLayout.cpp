#include "UI/Chrome/WindowChromeLayout.h"

#include <algorithm>

namespace Zenvra::UI::Chrome
{

namespace
{

constexpr std::array<std::string_view, window_menu_count> menu_labels{
    "File",
    "Edit",
    "Selection",
    "View",
    "Navigate",
    "Project",
    "Build",
    "Run",
    "Window",
    "Help",
};

} // namespace

bool WindowChromeLayoutResult::is_drag_region(float point_x, float point_y) const noexcept
{
    if (!titlebar_bounds.contains(point_x, point_y) || logo_bounds.contains(point_x, point_y) ||
        command_center_bounds.contains(point_x, point_y) || is_overflow_menu(point_x, point_y) ||
        get_window_control(point_x, point_y) != WindowControl::NoControl ||
        get_menu_index(point_x, point_y).has_value())
    {
        return false;
    }

    return true;
}

bool WindowChromeLayoutResult::has_overflow_menu() const noexcept
{
    return first_overflow_menu_index < window_menu_count && !overflow_menu_bounds.is_empty();
}

bool WindowChromeLayoutResult::is_overflow_menu(float point_x, float point_y) const noexcept
{
    return has_overflow_menu() && overflow_menu_bounds.contains(point_x, point_y);
}

WindowControl WindowChromeLayoutResult::get_window_control(float point_x, float point_y) const noexcept
{
    if (minimize_bounds.contains(point_x, point_y))
    {
        return WindowControl::Minimize;
    }
    if (maximize_bounds.contains(point_x, point_y))
    {
        return WindowControl::MaximizeRestore;
    }
    if (close_bounds.contains(point_x, point_y))
    {
        return WindowControl::Close;
    }
    return WindowControl::NoControl;
}

std::optional<std::size_t> WindowChromeLayoutResult::get_menu_index(float point_x, float point_y) const noexcept
{
    for (std::size_t index = 0; index < visible_menu_count; ++index)
    {
        if (menu_regions[index].bounds.contains(point_x, point_y))
        {
            return menu_regions[index].menu_index;
        }
    }
    return std::nullopt;
}

WindowChromeLayoutResult WindowChromeLayout::calculate(
    float client_width,
    float dpi_scale,
    WindowChromeLayoutOptions options) const noexcept
{
    const float safe_scale = std::max(dpi_scale, 0.5F);
    const WindowChromeMetrics metrics;
    const float titlebar_height = metrics.titlebar_height * safe_scale;
    const float control_width = metrics.window_control_width * safe_scale;
    const float horizontal_padding = metrics.horizontal_padding * safe_scale;

    WindowChromeLayoutResult result;
    result.dpi_scale = safe_scale;
    result.titlebar_bounds = {0.0F, 0.0F, std::max(client_width, 0.0F), titlebar_height};
    result.logo_bounds = {0.0F, 0.0F, metrics.logo_width * safe_scale, titlebar_height};

    const float controls_start = options.show_window_controls
        ? std::max(client_width - control_width * 3.0F, result.logo_bounds.right())
        : std::max(client_width, result.logo_bounds.right());
    if (options.show_window_controls)
    {
        result.minimize_bounds = {controls_start, 0.0F, control_width, titlebar_height};
        result.maximize_bounds = {controls_start + control_width, 0.0F, control_width, titlebar_height};
        result.close_bounds = {controls_start + control_width * 2.0F, 0.0F, control_width, titlebar_height};
    }

    const float command_area_end = controls_start - horizontal_padding;
    if (client_width >= metrics.command_center_minimum_window_width * safe_scale)
    {
        const float minimum_command_width = metrics.command_center_minimum_width * safe_scale;
        const float maximum_command_width = metrics.command_center_width * safe_scale;
        const float responsive_command_width = client_width * 0.30F;
        const float available_command_width = std::max(
            command_area_end - result.logo_bounds.right() - horizontal_padding,
            0.0F);
        const float command_width = std::min(
            std::clamp(responsive_command_width, minimum_command_width, maximum_command_width),
            available_command_width);
        if (command_width >= minimum_command_width)
        {
            const float centered_x = client_width * 0.5F - command_width * 0.5F;
            const float command_x = std::min(centered_x, command_area_end - command_width);
            const float vertical_padding = 5.0F * safe_scale;
            result.command_center_bounds = {
                command_x,
                vertical_padding,
                command_width,
                titlebar_height - vertical_padding * 2.0F,
            };
        }
    }

    float menu_x = result.logo_bounds.right();
    const float menu_limit = result.command_center_bounds.is_empty()
        ? command_area_end
        : result.command_center_bounds.x - horizontal_padding;
    std::array<float, window_menu_count> menu_widths{};
    float complete_menu_width = 0.0F;
    for (std::size_t index = 0; index < menu_labels.size(); ++index)
    {
        menu_widths[index] = static_cast<float>(menu_labels[index].size()) * 7.0F * safe_scale +
            metrics.menu_item_padding * safe_scale;
        complete_menu_width += menu_widths[index];
    }

    const bool requires_overflow = menu_x + complete_menu_width > menu_limit;
    const float reserved_overflow_width = requires_overflow
        ? metrics.overflow_menu_width * safe_scale
        : 0.0F;
    for (std::size_t index = 0; index < menu_labels.size(); ++index)
    {
        if (menu_x + menu_widths[index] + reserved_overflow_width > menu_limit)
        {
            break;
        }

        result.menu_regions[result.visible_menu_count] = MenuRegion{
            .menu_index = index,
            .bounds = {menu_x, 0.0F, menu_widths[index], titlebar_height},
        };
        ++result.visible_menu_count;
        menu_x += menu_widths[index];
    }

    if (result.visible_menu_count < window_menu_count &&
        menu_x + reserved_overflow_width <= menu_limit)
    {
        result.first_overflow_menu_index = result.visible_menu_count;
        result.overflow_menu_bounds = {
            menu_x,
            0.0F,
            reserved_overflow_width,
            titlebar_height,
        };
    }

    return result;
}

const std::array<std::string_view, window_menu_count>& WindowChromeLayout::get_menu_labels() noexcept
{
    return menu_labels;
}

} // namespace Zenvra::UI::Chrome
