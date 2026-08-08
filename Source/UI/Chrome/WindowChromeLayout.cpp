#include "UI/Chrome/WindowChromeLayout.h"

#include <algorithm>
#include <string_view>

namespace Zenvra::UI::Chrome {

namespace {

constexpr std::array<std::string_view, window_menu_count> menu_labels{
    "File",    "Edit",  "Selection", "View",   "Navigate",
    "Project", "Build", "Run",       "Window", "Help",
};

} // namespace

bool WindowChromeLayoutResult::is_drag_region(float point_x,
                                              float point_y) const noexcept {
  if (!titlebar_bounds.contains(point_x, point_y) ||
      logo_bounds.contains(point_x, point_y) ||
      command_center_bounds.contains(point_x, point_y) ||
      is_overflow_menu(point_x, point_y) ||
      get_window_control(point_x, point_y) != WindowControl::NoControl ||
      get_menu_index(point_x, point_y).has_value() ||
      run_bounds.contains(point_x, point_y) ||
      debug_bounds.contains(point_x, point_y) ||
      ellipsis_bounds.contains(point_x, point_y) ||
      compiler_bounds.contains(point_x, point_y) ||
      platform_bounds.contains(point_x, point_y) ||
      binary_bounds.contains(point_x, point_y) ||
      build_bounds.contains(point_x, point_y) ||
      gear_bounds.contains(point_x, point_y)) {
    return false;
  }

  return true;
}

bool WindowChromeLayoutResult::has_overflow_menu() const noexcept {
  return first_overflow_menu_index < window_menu_count &&
         !overflow_menu_bounds.is_empty();
}

bool WindowChromeLayoutResult::is_overflow_menu(float point_x,
                                                float point_y) const noexcept {
  return has_overflow_menu() && overflow_menu_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_run_button(float point_x,
                                             float point_y) const noexcept {
  return run_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_debug_button(float point_x,
                                               float point_y) const noexcept {
  return debug_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_ellipsis_button(
    float point_x, float point_y) const noexcept {
  return ellipsis_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_compiler_button(
    float point_x, float point_y) const noexcept {
  return compiler_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_platform_button(
    float point_x, float point_y) const noexcept {
  return platform_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_binary_button(float point_x,
                                                float point_y) const noexcept {
  return binary_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_build_button(float point_x,
                                               float point_y) const noexcept {
  return build_bounds.contains(point_x, point_y);
}

bool WindowChromeLayoutResult::is_gear_button(float point_x,
                                              float point_y) const noexcept {
  return gear_bounds.contains(point_x, point_y);
}

WindowControl
WindowChromeLayoutResult::get_window_control(float point_x,
                                             float point_y) const noexcept {
  if (minimize_bounds.contains(point_x, point_y)) {
    return WindowControl::Minimize;
  }
  if (maximize_bounds.contains(point_x, point_y)) {
    return WindowControl::MaximizeRestore;
  }
  if (close_bounds.contains(point_x, point_y)) {
    return WindowControl::Close;
  }
  return WindowControl::NoControl;
}

std::optional<std::size_t>
WindowChromeLayoutResult::get_menu_index(float point_x,
                                         float point_y) const noexcept {
  for (std::size_t index = 0; index < visible_menu_count; ++index) {
    if (menu_regions[index].bounds.contains(point_x, point_y)) {
      return menu_regions[index].menu_index;
    }
  }

  if (compiler_bounds.contains(point_x, point_y)) return 10;
  if (platform_bounds.contains(point_x, point_y)) return 11;
  if (binary_bounds.contains(point_x, point_y)) return 12;
  if (gear_bounds.contains(point_x, point_y)) return 13;
  if (ellipsis_bounds.contains(point_x, point_y)) return 14;

  return std::nullopt;
}

WindowChromeLayoutResult WindowChromeLayout::calculate(
    float client_width, float dpi_scale,
    WindowChromeLayoutOptions options) const noexcept {
  const float safe_scale = std::max(dpi_scale, 0.5F);
  const WindowChromeMetrics metrics;
  const float titlebar_height = metrics.titlebar_height * safe_scale;
  const float control_width = metrics.window_control_width * safe_scale;

  WindowChromeLayoutResult result;
  result.dpi_scale = safe_scale;
  result.titlebar_bounds = {0.0F, 0.0F, std::max(client_width, 0.0F),
                            titlebar_height};
  result.logo_bounds = {0.0F, 0.0F, metrics.logo_width * safe_scale,
                        titlebar_height};

  const float controls_start =
      options.show_window_controls
          ? std::max(client_width - control_width * 3.0F,
                     result.logo_bounds.right())
          : std::max(client_width, result.logo_bounds.right());
  if (options.show_window_controls) {
    result.minimize_bounds = {controls_start, 0.0F, control_width,
                              titlebar_height};
    result.maximize_bounds = {controls_start + control_width, 0.0F,
                              control_width, titlebar_height};
    result.close_bounds = {controls_start + control_width * 2.0F, 0.0F,
                           control_width, titlebar_height};
  }

  // Keep all top-level labels in the custom overlay. The titlebar remains a
  // compact hamburger anchor, leaving the full central span available for
  // editor tabs without any menu text underneath them.
  const float hamburger_width =
      std::min(metrics.overflow_menu_width * safe_scale,
               std::max(controls_start - result.logo_bounds.right(), 0.0F));
  if (hamburger_width > 0.0F) {
    result.first_overflow_menu_index = 0;
    result.overflow_menu_bounds = {
        result.logo_bounds.right(),
        0.0F,
        hamburger_width,
        titlebar_height,
    };
  }

  const float button_width = 36.0F * safe_scale;
  float current_right = controls_start;
  // Right-to-left: ellipsis | gear | debug | run | build | mode | binary |
  // compiler
  if (current_right - button_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= button_width;
    result.ellipsis_bounds = {current_right, 0.0F, button_width,
                              titlebar_height};
  }
  if (current_right - button_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= button_width;
    result.gear_bounds = {current_right, 0.0F, button_width, titlebar_height};
  }
  if (current_right - button_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= button_width;
    result.debug_bounds = {current_right, 0.0F, button_width, titlebar_height};
  }
  if (current_right - button_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= button_width;
    result.run_bounds = {current_right, 0.0F, button_width, titlebar_height};
  }
  if (current_right - button_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= button_width;
    result.build_bounds = {current_right, 0.0F, button_width, titlebar_height};
  }

  const float binary_width = 120.0F * safe_scale;
  const float platform_width = 72.0F * safe_scale;
  const float compiler_width = 80.0F * safe_scale;

  if (current_right - binary_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= binary_width;
    result.binary_bounds = {current_right, 0.0F, binary_width, titlebar_height};
  }
  if (current_right - platform_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= platform_width;
    result.platform_bounds = {current_right, 0.0F, platform_width,
                              titlebar_height};
  }
  if (current_right - compiler_width >=
      result.logo_bounds.right() + hamburger_width) {
    current_right -= compiler_width;
    result.compiler_bounds = {current_right, 0.0F, compiler_width,
                              titlebar_height};
  }

  return result;
}

const std::array<std::string_view, window_menu_count> &
WindowChromeLayout::get_menu_labels() noexcept {
  return menu_labels;
}

} // namespace Zenvra::UI::Chrome
