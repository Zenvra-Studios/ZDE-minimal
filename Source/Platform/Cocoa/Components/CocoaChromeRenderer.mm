#include "Platform/Cocoa/Components/CocoaChromeRenderer.h"
#include "Platform/HostSystem.h"
#include "Utility/Fonts.h"

#include <cmath>

namespace Zenvra::Platform::Cocoa::Components {

namespace {
int round_to_int(float v) { return static_cast<int>(std::lround(v)); }
}

void CocoaChromeRenderer::color_to_rgba(const UI::Theme::Color &color,
                                        CGFloat *rgba) {
  rgba[0] = static_cast<CGFloat>(color.red) / 255.0;
  rgba[1] = static_cast<CGFloat>(color.green) / 255.0;
  rgba[2] = static_cast<CGFloat>(color.blue) / 255.0;
  rgba[3] = static_cast<CGFloat>(color.alpha) / 255.0;
}

CocoaChromeRenderer::CocoaChromeRenderer()
{
  const auto arch = Platform::HostSystem::get_native_architecture();
  if (arch == Platform::HostSystem::Architecture::Arm64) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::Arm64;
  } else if (arch == Platform::HostSystem::Architecture::X86_64) {
    m_run_config_state.active_architecture = UI::Toolbar::TargetArchitecture::X86_64;
  }
  m_run_config_state.active_preset_name = Platform::HostSystem::get_system_info().default_preset_debug;
}
CocoaChromeRenderer::~CocoaChromeRenderer() { shutdown(); }

bool CocoaChromeRenderer::initialize(float dpi_scale,
                                     const UI::Theme::StudioTheme &theme) {
  shutdown();
  m_dpi_scale = std::max(dpi_scale, 0.5F);

  if (!m_workspace_renderer.initialize(dpi_scale))
    return false;

  // Use the macOS system UI font (San Francisco) for menus and toolbars
  const float menu_size = std::max(12.0F * m_dpi_scale, 9.0F);
  m_font = std::make_unique<AntialiasedFont>(".AppleSystemUIFont", menu_size);
  if (!m_font->isValid()) {
    m_font = std::make_unique<AntialiasedFont>("Helvetica", menu_size);
  }
  if (!m_font->isValid()) {
    shutdown();
    return false;
  }

  m_theme = theme;
  color_to_rgba(theme.window_background, m_colors.window_background);
  color_to_rgba(theme.titlebar_background, m_colors.titlebar_background);
  color_to_rgba(theme.titlebar_border, m_colors.titlebar_border);
  color_to_rgba(theme.text_primary, m_colors.text_primary);
  color_to_rgba(theme.text_secondary, m_colors.text_secondary);
  color_to_rgba(theme.hover, m_colors.hover);
  color_to_rgba(theme.pressed, m_colors.pressed);
  color_to_rgba(theme.accent, m_colors.accent);
  color_to_rgba(theme.command_center_background,
                m_colors.command_center_background);
  color_to_rgba(theme.command_center_border, m_colors.command_center_border);
  color_to_rgba(theme.close_hover, m_colors.close_hover);
  color_to_rgba(theme.panel_background, m_colors.popup_background);
  color_to_rgba(theme.titlebar_border, m_colors.popup_border);

  m_titlebar_background_color = theme.titlebar_background;
  m_hover_color = theme.hover;

  char hex[8]{};
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", theme.text_primary.red,
                theme.text_primary.green, theme.text_primary.blue);
  m_text_colors.primary = hex;
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", theme.text_secondary.red,
                theme.text_secondary.green, theme.text_secondary.blue);
  m_text_colors.secondary = hex;
  m_text_colors.white = "#ffffff";

  return true;
}

void CocoaChromeRenderer::shutdown() {
  m_workspace_renderer.shutdown();
  m_font.reset();
}

const std::filesystem::path &
CocoaChromeRenderer::get_icon_asset_root() const noexcept {
  return m_workspace_renderer.get_icon_asset_root();
}
bool CocoaChromeRenderer::open_workspace_file(
    const std::filesystem::path &path) {
  return m_workspace_renderer.open_file(path);
}
bool CocoaChromeRenderer::set_workspace_root(
    const std::filesystem::path &root) {
  return m_workspace_renderer.set_workspace_root(root);
}
std::size_t CocoaChromeRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> paths) {
  return m_workspace_renderer.open_dropped_paths(paths);
}
bool CocoaChromeRenderer::create_workspace_buffer() {
  return m_workspace_renderer.create_buffer();
}
bool CocoaChromeRenderer::toggle_terminal() {
  return m_workspace_renderer.toggle_terminal();
}
bool CocoaChromeRenderer::toggle_shader_sandbox() {
  return m_workspace_renderer.toggle_shader_sandbox();
}
void CocoaChromeRenderer::set_fullscreen(bool fullscreen) noexcept {
  m_workspace_renderer.set_fullscreen(fullscreen);
}
bool CocoaChromeRenderer::is_fullscreen() const noexcept {
  return m_workspace_renderer.is_fullscreen();
}

bool CocoaChromeRenderer::handle_workspace_pointer_press(float px, float py,
                                                         int cw, int ch,
                                                         float ct, bool ex,
                                                         int cc, double et,
                                                         std::string &cmd) {
  return m_workspace_renderer.handle_pointer_press(px, py, cw, ch, ct, ex, cc,
                                                   et, cmd);
}
bool CocoaChromeRenderer::handle_workspace_pointer_move(float px, float py,
                                                        int cw, int ch,
                                                        float ct) noexcept {
  return m_workspace_renderer.handle_pointer_move(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::handle_workspace_pointer_drag(float px, float py,
                                                        int cw, int ch,
                                                        float ct) {
  return m_workspace_renderer.handle_pointer_drag(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::handle_workspace_pointer_release() noexcept {
  return m_workspace_renderer.handle_pointer_release();
}
bool CocoaChromeRenderer::handle_workspace_scroll(float px, float py,
                                                  std::string &cmd,
                                                  std::ptrdiff_t d, bool h,
                                                  int cw, int ch,
                                                  float ct) noexcept {
  return m_workspace_renderer.handle_scroll(px, py, cmd, d, h, cw, ch, ct);
}

bool CocoaChromeRenderer::handle_editor_input(UI::Editor::EditorInputCommand c,
                                              bool e) {
  return m_workspace_renderer.handle_editor_input(c, e);
}
bool CocoaChromeRenderer::handle_editor_action(UI::Editor::EditorAction a) {
  return m_workspace_renderer.handle_editor_action(a);
}
std::optional<bool>
CocoaChromeRenderer::handle_editor_command(std::string_view c) {
  return m_workspace_renderer.handle_editor_command(c);
}
std::optional<bool> CocoaChromeRenderer::is_editor_command_enabled(
    std::string_view c) const noexcept {
  return m_workspace_renderer.is_editor_command_enabled(c);
}
bool CocoaChromeRenderer::handle_text_input(std::string_view t) {
  return m_workspace_renderer.handle_text_input(t);
}
bool CocoaChromeRenderer::handle_terminal_key(Terminal::TerminalInputKey k) {
  return m_workspace_renderer.handle_terminal_key(k);
}
bool CocoaChromeRenderer::handle_terminal_control(char l) {
  return m_workspace_renderer.handle_terminal_control(l);
}
bool CocoaChromeRenderer::handle_terminal_scroll(std::ptrdiff_t d,
                                                 bool h) noexcept {
  return m_workspace_renderer.handle_terminal_scroll(d, h);
}
bool CocoaChromeRenderer::handle_tool_sidebar_scroll(std::ptrdiff_t d, int cw,
                                                     int ch,
                                                     float ct) noexcept {
  return m_workspace_renderer.handle_tool_sidebar_scroll(d, cw, ch, ct);
}
bool CocoaChromeRenderer::is_editor_focused() const noexcept {
  return m_workspace_renderer.is_editor_focused();
}
bool CocoaChromeRenderer::is_terminal_focused() const noexcept {
  return m_workspace_renderer.is_terminal_focused();
}
bool CocoaChromeRenderer::is_activity_bar_point(float px, float py, int cw,
                                                int ch,
                                                float ct) const noexcept {
  return m_workspace_renderer.is_activity_bar_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_tab_bar_point(float px, float py, int cw, int ch,
                                           float ct) const noexcept {
  return m_workspace_renderer.is_tab_bar_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_tab_bar_area_point(float px, float py, int cw,
                                                int ch,
                                                float ct) const noexcept {
  return m_workspace_renderer.is_tab_bar_area_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_editor_point(float px, float py, int cw, int ch,
                                          float ct) const noexcept {
  return m_workspace_renderer.is_editor_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_scrollbar_point(float px, float py, int cw, int ch,
                                             float ct) const noexcept {
  return m_workspace_renderer.is_scrollbar_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_minimap_point(float px, float py, int cw, int ch,
                                           float ct) const noexcept {
  return m_workspace_renderer.is_minimap_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_fold_margin_point(float px, float py, int cw,
                                               int ch,
                                               float ct) const noexcept {
  return m_workspace_renderer.is_fold_margin_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_terminal_point(float px, float py, int cw, int ch,
                                            float ct) const noexcept {
  return m_workspace_renderer.is_terminal_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_tool_sidebar_point(float px, float py, int cw,
                                                int ch,
                                                float ct) const noexcept {
  return m_workspace_renderer.is_tool_sidebar_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_terminal_resize_handle_point(
    float px, float py, int cw, int ch, float ct) const noexcept {
  return m_workspace_renderer.is_terminal_resize_handle_point(px, py, cw, ch,
                                                              ct);
}
bool CocoaChromeRenderer::is_terminal_resizing() const noexcept {
  return m_workspace_renderer.is_terminal_resizing();
}
bool CocoaChromeRenderer::is_editor_interactive_point(float px,
                                                      float py) const noexcept {
  return m_workspace_renderer.is_editor_interactive_point(px, py);
}
bool CocoaChromeRenderer::is_terminal_interactive_point(
    float px, float py, int cw, int ch, float ct) const noexcept {
  return m_workspace_renderer.is_terminal_interactive_point(px, py, cw, ch, ct);
}
bool CocoaChromeRenderer::is_sidebar_resize_handle_point(
    float px, float py, int cw, int ch, float ct) const noexcept {
  return m_workspace_renderer.is_sidebar_resize_handle_point(px, py, cw, ch,
                                                             ct);
}
bool CocoaChromeRenderer::is_sidebar_resizing() const noexcept {
  return m_workspace_renderer.is_sidebar_resizing();
}
bool CocoaChromeRenderer::is_empty_state_button_hovered() const noexcept {
  return m_workspace_renderer.is_empty_state_button_hovered();
}
bool CocoaChromeRenderer::tick_animations() noexcept {
  return m_workspace_renderer.tick_animations();
}

bool CocoaChromeRenderer::update_chrome_hover_state(
    float point_x, float point_y,
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    ChromeInteractionState &state) const noexcept {
  ChromeInteractionState updated = state;
  updated.hovered_menu_index.reset();
  updated.hovered_popup_item_index.reset();
  updated.overflow_menu_hovered = false;
  updated.command_center_hovered = false;
  updated.run_button_hovered = false;
  updated.debug_button_hovered = false;
  updated.ellipsis_button_hovered = false;
  updated.compiler_button_hovered = false;
  updated.platform_button_hovered = false;
  updated.binary_button_hovered = false;
  updated.mode_button_hovered = false;
  updated.build_button_hovered = false;
  updated.gear_button_hovered = false;

  if (chrome_layout.titlebar_bounds.contains(point_x, point_y)) {
    for (std::size_t region_index = 0;
         region_index < chrome_layout.visible_menu_count; ++region_index) {
      const UI::Chrome::MenuRegion &region =
          chrome_layout.menu_regions[region_index];
      if (region.bounds.contains(point_x, point_y)) {
        updated.hovered_menu_index = region.menu_index;
        break;
      }
    }
    if (chrome_layout.overflow_menu_bounds.contains(point_x, point_y)) {
      updated.overflow_menu_hovered = true;
    }
    if (chrome_layout.command_center_bounds.contains(point_x, point_y)) {
      updated.command_center_hovered = true;
    }
    if (chrome_layout.run_bounds.contains(point_x, point_y)) {
      updated.run_button_hovered = true;
    }
    if (chrome_layout.debug_bounds.contains(point_x, point_y)) {
      updated.debug_button_hovered = true;
    }
    if (chrome_layout.ellipsis_bounds.contains(point_x, point_y)) {
      updated.ellipsis_button_hovered = true;
    }
    if (chrome_layout.compiler_bounds.contains(point_x, point_y)) {
      updated.compiler_button_hovered = true;
    }
    if (chrome_layout.platform_bounds.contains(point_x, point_y)) {
      updated.platform_button_hovered = true;
    }
    if (chrome_layout.binary_bounds.contains(point_x, point_y)) {
      updated.binary_button_hovered = true;
    }
    if (chrome_layout.mode_bounds.contains(point_x, point_y)) {
      updated.mode_button_hovered = true;
    }
    if (chrome_layout.build_bounds.contains(point_x, point_y)) {
      updated.build_button_hovered = true;
    }
    if (chrome_layout.gear_bounds.contains(point_x, point_y)) {
      updated.gear_button_hovered = true;
    }
  }

  if (updated == state) {
    return false;
  }
  state = updated;
  return true;
}

void CocoaChromeRenderer::render(
    CGContextRef context, int client_width, int client_height,
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    const ChromeInteractionState &interaction_state,
    const CommandStateQueryCallback &command_state_query_callback) const {
  (void)command_state_query_callback;
  // Draw titlebar background
  fill_rectangle(context, chrome_layout.titlebar_bounds,
                 m_colors.titlebar_background);

  // Note: Cocoa native window controls (traffic lights) are drawn by the OS.
  // We intentionally don't draw
  // UI::Chrome::WindowControl::Minimize/Maximize/Close here.

  auto draw_menu_hover = [&](const UI::Rect &bounds) {
      UI::Rect hover_bounds = bounds;
      hover_bounds.y += 4.0F * chrome_layout.dpi_scale;
      hover_bounds.height -= 8.0F * chrome_layout.dpi_scale;
      fill_rectangle(context, hover_bounds, m_colors.hover, 4);
  };

  auto draw_toolbar_hover = [&](const UI::Rect &bounds) {
      UI::Rect hover_bounds = bounds;
      hover_bounds.y += 4.0F * chrome_layout.dpi_scale;
      hover_bounds.height -= 8.0F * chrome_layout.dpi_scale;
      hover_bounds.x += 2.0F * chrome_layout.dpi_scale;
      hover_bounds.width -= 4.0F * chrome_layout.dpi_scale;
      fill_rectangle(context, hover_bounds, m_colors.hover, 4);
  };

  const float scale = chrome_layout.dpi_scale;

  // Draw Menus
  const std::span<const UI::Components::Menu> menus = UI::Components::get_window_menus();
  for (std::size_t region_index = 0; region_index < chrome_layout.visible_menu_count; ++region_index) {
      const UI::Chrome::MenuRegion &region = chrome_layout.menu_regions[region_index];
      const bool hovered = interaction_state.hovered_menu_index == region.menu_index ||
                           interaction_state.open_menu_index == region.menu_index;
      if (hovered) {
          draw_menu_hover(region.bounds);
      }
      if (region.menu_index < menus.size()) {
          draw_centered_text(context, menus[region.menu_index].label, region.bounds, m_text_colors.primary);
      }
  }

  // Draw Hamburger Menu
  if (chrome_layout.has_overflow_menu()) {
      const bool hidden_menu_open = interaction_state.open_menu_index &&
                                    *interaction_state.open_menu_index >= chrome_layout.first_overflow_menu_index;
      if (interaction_state.overflow_menu_hovered || interaction_state.overflow_menu_open || hidden_menu_open) {
          draw_menu_hover(chrome_layout.overflow_menu_bounds);
      }
      const int line_half_width = std::max(round_to_int(6.0F * scale), 4);
      const int line_gap = std::max(round_to_int(4.0F * scale), 3);
      const int center_x = round_to_int(chrome_layout.overflow_menu_bounds.x + chrome_layout.overflow_menu_bounds.width * 0.5F);
      const int center_y = round_to_int(chrome_layout.overflow_menu_bounds.y + chrome_layout.overflow_menu_bounds.height * 0.5F);
      for (int row = -1; row <= 1; ++row) {
          draw_line(context, center_x - line_half_width, center_y + row * line_gap,
                    center_x + line_half_width, center_y + row * line_gap, m_colors.text_primary);
      }
  }

  // Toolbar Icons
  auto draw_toolbar_icon = [&](const UI::Rect &bounds, bool hovered, const std::string& icon_name, const UI::Theme::Color& icon_color, float icon_sz = 16.0F) {
      if (bounds.is_empty()) return;
      if (hovered) draw_toolbar_hover(bounds);
      const int center_x = round_to_int(bounds.x + bounds.width * 0.5F);
      const int center_y = round_to_int(bounds.y + bounds.height * 0.5F);
      const int size = std::max(round_to_int(icon_sz * scale), 14);
      m_workspace_renderer.draw_svg_icon(context, "Assets/icons/" + icon_name, center_x, center_y, size,
          icon_color, hovered ? m_theme.hover : m_theme.titlebar_background);
  };

  const bool gear_hovered = interaction_state.gear_button_hovered || (interaction_state.open_menu_index && *interaction_state.open_menu_index == 13);
  const bool ellipsis_hovered = interaction_state.ellipsis_button_hovered || (interaction_state.open_menu_index && *interaction_state.open_menu_index == 14);

  draw_toolbar_icon(chrome_layout.build_bounds, interaction_state.build_button_hovered, "build.svg", m_theme.text_primary);
  draw_toolbar_icon(chrome_layout.run_bounds, interaction_state.run_button_hovered, "play.svg", UI::Theme::Color{152, 195, 121, 255}, 20.0F);
  draw_toolbar_icon(chrome_layout.debug_bounds, interaction_state.debug_button_hovered, "bug.svg", UI::Theme::Color{229, 192, 123, 255}, 18.0F);
  draw_toolbar_icon(chrome_layout.gear_bounds, gear_hovered, "gear.svg", m_theme.text_primary);
  draw_toolbar_icon(chrome_layout.ellipsis_bounds, ellipsis_hovered, "ellipsis.svg", m_theme.text_primary);

  // Combo Boxes
  auto draw_combo = [&](const UI::Rect &bounds, bool hovered, std::string_view text, bool is_binary) {
      if (bounds.is_empty()) return;
      if (hovered) draw_toolbar_hover(bounds);
      
      float text_left = 10.0F * scale;
      if (is_binary) {
          const int binary_icon_size = std::max(round_to_int(16.0F * scale), 14);
          m_workspace_renderer.draw_svg_icon(context, "Assets/icons/terminal.svg",
              round_to_int(bounds.x + 14.0F * scale),
              round_to_int(bounds.y + bounds.height * 0.5F),
              binary_icon_size, m_theme.text_primary,
              hovered ? m_theme.hover : m_theme.titlebar_background);
          text_left = 32.0F * scale;
      }
      
      draw_text(context, text, bounds, text_left, m_text_colors.primary);
      
      if (chrome_layout.show_toolbar_chevrons) {
          const int chevron_x = round_to_int(bounds.right() - 10.0F * scale);
          const int chevron_y = round_to_int(bounds.y + bounds.height * 0.5F);
          m_workspace_renderer.draw_svg_icon(context, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
              std::max(round_to_int(10.0F * scale), 8),
              m_workspace_renderer.m_palette.text_muted,
              hovered ? m_theme.hover : m_theme.titlebar_background);
      }
  };
  
  const bool compiler_hovered = interaction_state.compiler_button_hovered || (interaction_state.open_menu_index && *interaction_state.open_menu_index == 10);
  const bool platform_hovered = interaction_state.platform_button_hovered || (interaction_state.open_menu_index && *interaction_state.open_menu_index == 11);
  const bool binary_hovered = interaction_state.binary_button_hovered || (interaction_state.open_menu_index && *interaction_state.open_menu_index == 12);

  draw_combo(chrome_layout.compiler_bounds, compiler_hovered, UI::Toolbar::to_string(m_run_config_state.active_mode), false);
  draw_combo(chrome_layout.platform_bounds, platform_hovered, UI::Toolbar::to_string(m_run_config_state.active_architecture), false);
  draw_combo(chrome_layout.binary_bounds, binary_hovered, m_run_config_state.active_target_name, true);

  // Command Center / File Buffer: intentionally not drawn on macOS.
  // The integrated tab strip (workspace) fills the titlebar span between the
  // logo and the toolbar; Win32/X11 draw nothing here either, so rendering the
  // pill or the centered buffer name would overlap ("numpuk") the tabs.

  // Draw the actual workspace content below the titlebar
  m_workspace_renderer.render(context, client_width, client_height,
                              chrome_layout.titlebar_bounds.bottom());

  // Top border of content area (separates titlebar from content)
  draw_line(context, 0, round_to_int(chrome_layout.titlebar_bounds.bottom()), client_width,
            round_to_int(chrome_layout.titlebar_bounds.bottom()), m_colors.titlebar_border);
}

void CocoaChromeRenderer::fill_rectangle(CGContextRef context,
                                         const UI::Rect &rectangle,
                                         const CGFloat *rgba,
                                         int radius) const {
  if (rectangle.is_empty())
    return;
  CGContextSetRGBFillColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
  if (radius > 0) {
    CGRect cg_rect =
        CGRectMake(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
    CGPathRef path =
        CGPathCreateWithRoundedRect(cg_rect, radius, radius, nullptr);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGPathRelease(path);
  } else {
    CGContextFillRect(context, CGRectMake(rectangle.x, rectangle.y,
                                          rectangle.width, rectangle.height));
  }
}

void CocoaChromeRenderer::draw_rectangle(CGContextRef context,
                                         const UI::Rect &rectangle,
                                         const CGFloat *rgba,
                                         int radius) const {
  if (rectangle.is_empty())
    return;
  CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
  if (radius > 0) {
    CGRect cg_rect =
        CGRectMake(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
    CGPathRef path =
        CGPathCreateWithRoundedRect(cg_rect, radius, radius, nullptr);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextStrokePath(context);
    CGPathRelease(path);
  } else {
    CGContextStrokeRect(context, CGRectMake(rectangle.x, rectangle.y,
                                            rectangle.width, rectangle.height));
  }
}

void CocoaChromeRenderer::draw_centered_text(CGContextRef context,
                                             std::string_view text,
                                             const UI::Rect &rectangle,
                                             const std::string &color) const {
  if (text.empty() || !m_font)
    return;
  const float text_width =
      static_cast<float>(m_font->getTextWidth(std::string{text}));
  const float point_x = rectangle.x + (rectangle.width - text_width) * 0.5F;
  const float center_y = rectangle.y + rectangle.height * 0.5F;
  const int baseline = round_to_int(
      center_y -
      static_cast<float>(m_font->getAscent() + m_font->getDescent()) * 0.5F +
      static_cast<float>(m_font->getAscent()));

  CGRect clip =
      CGRectMake(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
  m_font->drawString(context, color, round_to_int(point_x), baseline,
                     std::string{text}, &clip);
}

void CocoaChromeRenderer::draw_text(CGContextRef context, std::string_view text,
                                    const UI::Rect &rectangle,
                                    float left_padding,
                                    const std::string &color) const {
  if (text.empty() || !m_font)
    return;
  const float point_x = rectangle.x + left_padding;
  const float center_y = rectangle.y + rectangle.height * 0.5F;
  const int baseline = round_to_int(
      center_y -
      static_cast<float>(m_font->getAscent() + m_font->getDescent()) * 0.5F +
      static_cast<float>(m_font->getAscent()));

  CGRect clip =
      CGRectMake(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
  m_font->drawString(context, color, round_to_int(point_x), baseline,
                     std::string{text}, &clip);
}

void CocoaChromeRenderer::draw_line(CGContextRef context, int from_x,
                                    int from_y, int to_x, int to_y,
                                    const CGFloat *rgba) const {
  CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
  CGContextSetLineWidth(context, 1.0);
  CGContextBeginPath(context);
  CGContextMoveToPoint(context, static_cast<CGFloat>(from_x) + 0.5,
                       static_cast<CGFloat>(from_y) + 0.5);
  CGContextAddLineToPoint(context, static_cast<CGFloat>(to_x) + 0.5,
                          static_cast<CGFloat>(to_y) + 0.5);
  CGContextStrokePath(context);
}

OverflowMenuGeometry CocoaChromeRenderer::calculate_overflow_menu_geometry(
    const UI::Chrome::WindowChromeLayoutResult &) const noexcept {
  return {};
}

PopupMenuGeometry CocoaChromeRenderer::calculate_popup_geometry(
    const UI::Chrome::WindowChromeLayoutResult &, std::size_t,
    bool) const noexcept {
  return {};
}

std::optional<std::size_t> CocoaChromeRenderer::get_popup_item_index(
    float, float,
    const UI::Chrome::WindowChromeLayoutResult &,
    const ChromeInteractionState &) const noexcept {
  return std::nullopt;
}

void CocoaChromeRenderer::draw_popup_menu(
    CGContextRef,
    const UI::Chrome::WindowChromeLayoutResult &,
    const ChromeInteractionState &,
    const CommandStateQueryCallback &) const {}

} // namespace Zenvra::Platform::Cocoa::Components
