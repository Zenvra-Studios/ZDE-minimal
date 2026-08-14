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
  const float titlebar_height = options.show_titlebar
      ? ((options.titlebar_height > 0.0F
               ? options.titlebar_height
               : (options.chrome_style == ChromeStyle::NativeMacOS
                      ? 28.0F
                      : metrics.titlebar_height)) *
          safe_scale)
      : 0.0F;
  const float control_width = metrics.window_control_width * safe_scale;

  WindowChromeLayoutResult result;
  result.dpi_scale = safe_scale;
  result.show_toolbar_chevrons = options.show_toolbar_chevrons;
  result.show_menu_labels = options.show_menu_labels;
  result.titlebar_bounds = {0.0F, 0.0F, std::max(client_width, 0.0F),
                            titlebar_height};

  if (options.chrome_style == ChromeStyle::NativeMacOS) {
    const float buffer_width = 300.0F * safe_scale;
    result.file_buffer_bounds = {
        std::max(0.0F, (client_width - buffer_width) * 0.5F),
        0.0F,
        std::min(client_width, buffer_width),
        titlebar_height};
    return result;
  }

  result.logo_bounds = {options.left_padding, 0.0F, metrics.logo_width * safe_scale,
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

  const float button_width = 36.0F * safe_scale;
  const float binary_width = 120.0F * safe_scale;
  const float platform_width = 72.0F * safe_scale;
  const float compiler_width = 80.0F * safe_scale;
  const float toolbar_gap = 8.0F * safe_scale;

  // The right-side toolbar (ellipsis | gear | debug | run | build | binary |
  // platform | compiler) has a fixed width. Reserve it up front so the menus
  // never extend underneath it and the elements never collide.
  const float right_toolbar_width =
      5.0F * button_width + binary_width + platform_width + compiler_width +
      7.0F * toolbar_gap;
  const float menu_limit = options.force_all_menus
      ? controls_start
      : controls_start - right_toolbar_width - toolbar_gap;

  // Layout Menus
  float current_x = result.logo_bounds.right();

  if (options.hamburger_only) {
      // All menus go behind the hamburger button; no inline labels.
      result.visible_menu_count = 0;
      result.first_overflow_menu_index = 0;
      result.show_menu_labels = false;
  } else {
      for (std::size_t i = 0; i < window_menu_count; ++i) {
          if (menu_labels[i].empty()) break;

          if (!options.show_menu_labels) {
              // macOS: the native menu bar owns the menus; nothing is drawn here.
              result.visible_menu_count = 0;
              break;
          }
          // Approximate width: ~8 pixels per character + padding
          float text_width = static_cast<float>(menu_labels[i].length()) * 8.0F * safe_scale;
          float menu_width = text_width + metrics.menu_item_padding * 2.0F * safe_scale;

          if (options.force_all_menus) {
              // Every menu stays inline; the toolbar gives up space instead of
              // pushing menus into a hamburger button.
              result.menu_regions[i] = {i, {current_x, 0.0F, menu_width, titlebar_height}};
              current_x += menu_width;
              result.visible_menu_count++;
              continue;
          }
          
          if (current_x + menu_width > menu_limit) {
              result.first_overflow_menu_index = i;
              break;
          }
          
          result.menu_regions[i] = {i, {current_x, 0.0F, menu_width, titlebar_height}};
          current_x += menu_width;
          result.visible_menu_count++;
      }
  }

  const float hamburger_width =
      options.hamburger_only ||
      (options.show_menu_labels && !options.force_all_menus &&
       result.visible_menu_count < window_menu_count)
          ? metrics.overflow_menu_width * safe_scale
          : 0.0F;
  if (hamburger_width > 0.0F && current_x + hamburger_width <= controls_start) {
    result.overflow_menu_bounds = {current_x, 0.0F, hamburger_width, titlebar_height};
  }

  const float left_edge = result.has_overflow_menu() ? result.overflow_menu_bounds.right() : current_x;

  // Right-to-left toolbar placement with a gap between every element.
  float current_right = controls_start;
  auto place_right = [&](float width, UI::Rect& target) -> bool {
    const float candidate_right = current_right - toolbar_gap;
    const float candidate_x = candidate_right - width;
    if (candidate_x < left_edge) {
      return false;
    }
    target = {candidate_x, 0.0F, width, titlebar_height};
    current_right = candidate_x;
    return true;
  };

  place_right(button_width, result.ellipsis_bounds);
  place_right(button_width, result.gear_bounds);
  place_right(button_width, result.debug_bounds);
  place_right(button_width, result.run_bounds);
  place_right(button_width, result.build_bounds);
  place_right(binary_width, result.binary_bounds);
  place_right(platform_width, result.platform_bounds);
  place_right(compiler_width, result.compiler_bounds);
  
  if (current_right > left_edge) {
      float available_space = current_right - left_edge;
      float center_x = client_width * 0.5F;
      float cc_width = metrics.command_center_width * safe_scale;
      
      if (available_space > cc_width && center_x - cc_width * 0.5F > left_edge && center_x + cc_width * 0.5F < current_right) {
          result.command_center_bounds = {center_x - cc_width * 0.5F, 0.0F, cc_width, titlebar_height};
          // File buffer takes remaining space
          result.file_buffer_bounds = {left_edge, 0.0F, result.command_center_bounds.x - left_edge, titlebar_height};
      } else {
          result.file_buffer_bounds = {left_edge, 0.0F, available_space, titlebar_height};
      }
  }

  return result;
}

const std::array<std::string_view, window_menu_count> &
WindowChromeLayout::get_menu_labels() noexcept {
  return menu_labels;
}

} // namespace Zenvra::UI::Chrome
