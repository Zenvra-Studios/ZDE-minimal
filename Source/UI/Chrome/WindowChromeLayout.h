#pragma once

#include "UI/Geometry.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace Zenvra::UI::Chrome
{

inline constexpr std::size_t window_menu_count = 10;

enum class WindowControl
{
    NoControl,
    Minimize,
    MaximizeRestore,
    Close,
};

struct WindowChromeMetrics
{
    float titlebar_height = 35.0F;
    float logo_width = 42.0F;
    float window_control_width = 46.0F;
    float command_center_width = 360.0F;
    float command_center_minimum_width = 180.0F;
    float command_center_minimum_window_width = 720.0F;
    float horizontal_padding = 8.0F;
    float menu_item_padding = 16.0F;
    float overflow_menu_width = 34.0F;
};

struct MenuRegion
{
    std::size_t menu_index = 0;
    Rect bounds;
};

struct WindowChromeLayoutOptions
{
    bool show_window_controls = true;
};

struct WindowChromeLayoutResult
{
    Rect titlebar_bounds;
    Rect logo_bounds;
    Rect command_center_bounds;
    Rect overflow_menu_bounds;
    Rect minimize_bounds;
    Rect maximize_bounds;
    Rect close_bounds;
    std::array<MenuRegion, window_menu_count> menu_regions{};
    std::size_t visible_menu_count = 0;
    std::size_t first_overflow_menu_index = window_menu_count;
    float dpi_scale = 1.0F;

    [[nodiscard]] bool has_overflow_menu() const noexcept;
    [[nodiscard]] bool is_overflow_menu(float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_drag_region(float point_x, float point_y) const noexcept;
    [[nodiscard]] WindowControl get_window_control(float point_x, float point_y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> get_menu_index(float point_x, float point_y) const noexcept;
};

class WindowChromeLayout
{
public:
    [[nodiscard]] WindowChromeLayoutResult calculate(
        float client_width,
        float dpi_scale,
        WindowChromeLayoutOptions options = {}) const noexcept;
    [[nodiscard]] static const std::array<std::string_view, window_menu_count>& get_menu_labels() noexcept;
};

} // namespace Zenvra::UI::Chrome
