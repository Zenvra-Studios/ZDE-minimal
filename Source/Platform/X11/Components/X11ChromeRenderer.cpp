#include "Platform/X11/Components/X11ChromeRenderer.h"

#include "Utility/Fonts.h"
#include "Utility/X11Rounded.h"

#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Zenvra::Platform::X11::Components {

namespace {

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

std::string to_xft_color(const UI::Theme::Color &color) {
  char value[8]{};
  std::snprintf(value, sizeof(value), "#%02x%02x%02x",
                static_cast<unsigned int>(color.red),
                static_cast<unsigned int>(color.green),
                static_cast<unsigned int>(color.blue));
  return value;
}

unsigned long interpolate_x11_pixel(unsigned long bg, unsigned long fg,
                                    float t) {
  const float r1 = static_cast<float>((bg >> 16) & 0xFF);
  const float g1 = static_cast<float>((bg >> 8) & 0xFF);
  const float b1 = static_cast<float>(bg & 0xFF);
  const float r2 = static_cast<float>((fg >> 16) & 0xFF);
  const float g2 = static_cast<float>((fg >> 8) & 0xFF);
  const float b2 = static_cast<float>(fg & 0xFF);
  const unsigned int r = static_cast<unsigned int>(r1 + (r2 - r1) * t);
  const unsigned int g = static_cast<unsigned int>(g1 + (g2 - g1) * t);
  const unsigned int b = static_cast<unsigned int>(b1 + (b2 - b1) * t);
  return (r << 16) | (g << 8) | b;
}

std::string interpolate_hex_color(const std::string &bg, const std::string &fg,
                                  float t) {
  auto parse_hex = [](const std::string &hex) -> unsigned int {
    unsigned int val = 0;
    if (hex.length() >= 7 && hex[0] == '#') {
      unsigned int r = 0, g = 0, b = 0;
      std::sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
      val = (r << 16) | (g << 8) | b;
    }
    return val;
  };
  unsigned int c1 = parse_hex(bg);
  unsigned int c2 = parse_hex(fg);
  const float r1 = static_cast<float>((c1 >> 16) & 0xFF);
  const float g1 = static_cast<float>((c1 >> 8) & 0xFF);
  const float b1 = static_cast<float>(c1 & 0xFF);
  const float r2 = static_cast<float>((c2 >> 16) & 0xFF);
  const float g2 = static_cast<float>((c2 >> 8) & 0xFF);
  const float b2 = static_cast<float>(c2 & 0xFF);
  const unsigned int r = static_cast<unsigned int>(r1 + (r2 - r1) * t);
  const unsigned int g = static_cast<unsigned int>(g1 + (g2 - g1) * t);
  const unsigned int b = static_cast<unsigned int>(b1 + (b2 - b1) * t);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return std::string(buf);
}

} // namespace

X11ChromeRenderer::X11ChromeRenderer() = default;

X11ChromeRenderer::~X11ChromeRenderer() { shutdown(); }

bool X11ChromeRenderer::initialize(Display *display, int screen,
                                   float dpi_scale,
                                   const UI::Theme::StudioTheme &theme) {
  shutdown();

  m_display = display;
  m_screen = screen;
  m_dpi_scale = std::max(dpi_scale, 0.5F);
  if (m_display == nullptr) {
    return false;
  }

  m_graphics_context =
      XCreateGC(m_display, RootWindow(m_display, m_screen), 0, nullptr);
  if (m_graphics_context == nullptr) {
    shutdown();
    return false;
  }

  char font_pattern[128]{};
  const int pixel_size = std::max(round_to_int(12.5F * m_dpi_scale), 10);
  std::snprintf(font_pattern, sizeof(font_pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight", pixel_size);
  m_font = std::make_unique<AntialiasedFont>(m_display, m_screen, font_pattern);
  if (m_font->getHeight() <= 0) {
    shutdown();
    return false;
  }

  XVisualInfo vinfo;
  if (XMatchVisualInfo(m_display, m_screen, 32, TrueColor, &vinfo)) {
    m_popup_depth = 32;
    m_popup_visual = vinfo.visual;
    m_popup_colormap = XCreateColormap(
        m_display, RootWindow(m_display, m_screen), m_popup_visual, AllocNone);

    // Create a 1x1 32-bit pixmap just to create the GC
    Pixmap temp_pm =
        XCreatePixmap(m_display, RootWindow(m_display, m_screen), 1, 1, 32);
    m_popup_graphics_context = XCreateGC(m_display, temp_pm, 0, nullptr);
    XFreePixmap(m_display, temp_pm);

    m_popup_font = std::make_unique<AntialiasedFont>(
        m_display, m_screen, font_pattern, m_popup_visual, m_popup_colormap);
  } else {
    m_popup_depth = CopyFromParent;
    m_popup_visual = CopyFromParent;
    m_popup_colormap = 0;
    m_popup_graphics_context = nullptr;
  }

  m_colors.window_background = allocate_color(theme.window_background);
  m_titlebar_background_color = theme.titlebar_background;
  m_hover_color = theme.hover;
  m_colors.titlebar_background = allocate_color(theme.titlebar_background);
  m_colors.titlebar_border = allocate_color(theme.titlebar_border);
  m_colors.text_primary = allocate_color(theme.text_primary);
  m_colors.text_secondary = allocate_color(theme.text_secondary);
  m_colors.hover = allocate_color(theme.hover);
  m_colors.pressed = allocate_color(theme.pressed);
  m_colors.accent = allocate_color(theme.accent);
  m_colors.command_center_background =
      allocate_color(theme.command_center_background);
  m_colors.command_center_border = allocate_color(theme.command_center_border);
  m_colors.close_hover = allocate_color(theme.close_hover);
  m_colors.popup_background = allocate_color(theme.panel_background);
  m_colors.popup_border = allocate_color(theme.titlebar_border);
  m_theme_popup_background = theme.panel_background;
  m_theme_popup_border = theme.titlebar_border;
  m_text_colors.primary = to_xft_color(theme.text_primary);
  m_text_colors.secondary = to_xft_color(theme.text_secondary);
  m_text_colors.white = "#ffffff";
  if (!m_workspace_renderer.initialize(m_display, m_screen, m_dpi_scale)) {
    shutdown();
    return false;
  }
  return true;
}

void X11ChromeRenderer::shutdown() {
  close_popup();
  m_workspace_renderer.shutdown();
  if (m_display != nullptr && m_back_buffer != 0) {
    XFreePixmap(m_display, m_back_buffer);
    m_back_buffer = 0;
    m_back_buffer_w = 0;
    m_back_buffer_h = 0;
  }
  if (m_display != nullptr && m_popup_back_buffer != 0) {
    XFreePixmap(m_display, m_popup_back_buffer);
    m_popup_back_buffer = 0;
    m_popup_back_buffer_w = 0;
    m_popup_back_buffer_h = 0;
  }
  // AntialiasedFont releases Xft resources through this display, so it must
  // be destroyed before the renderer forgets the connection.
  m_font.reset();
  m_popup_font.reset();
  if (m_display != nullptr && m_graphics_context != nullptr) {
    XFreeGC(m_display, m_graphics_context);
  }
  if (m_display != nullptr && m_popup_graphics_context != nullptr) {
    XFreeGC(m_display, m_popup_graphics_context);
  }
  if (m_display != nullptr && m_popup_colormap != 0) {
    XFreeColormap(m_display, m_popup_colormap);
  }

  m_graphics_context = nullptr;
  m_popup_graphics_context = nullptr;
  m_popup_colormap = 0;
  m_display = nullptr;
}

const std::filesystem::path &
X11ChromeRenderer::get_icon_asset_root() const noexcept {
  return m_workspace_renderer.get_icon_asset_root();
}

bool X11ChromeRenderer::open_workspace_file(const std::filesystem::path &path) {
  return m_workspace_renderer.open_file(path);
}

bool X11ChromeRenderer::set_workspace_root(const std::filesystem::path &root) {
  return m_workspace_renderer.set_workspace_root(root);
}

std::size_t X11ChromeRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths) {
  return m_workspace_renderer.open_dropped_paths(dropped_paths);
}

bool X11ChromeRenderer::create_workspace_buffer() {
  return m_workspace_renderer.create_buffer();
}

bool X11ChromeRenderer::handle_workspace_pointer_press(
    float point_x, float point_y, int client_width, int client_height,
    float content_top, bool extend_selection, int click_count, Time event_time,
    std::string &command_out) {
  return m_workspace_renderer.handle_pointer_press(
      point_x, point_y, client_width, client_height, content_top,
      extend_selection, click_count, event_time, command_out);
}

bool X11ChromeRenderer::handle_workspace_pointer_move(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) noexcept {
  return m_workspace_renderer.handle_pointer_move(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_workspace_pointer_drag(float point_x,
                                                      float point_y,
                                                      int client_width,
                                                      int client_height,
                                                      float content_top) {
  return m_workspace_renderer.handle_pointer_drag(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_workspace_pointer_release() noexcept {
  return m_workspace_renderer.handle_pointer_release();
}

bool X11ChromeRenderer::handle_workspace_scroll(
    float point_x, float point_y, std::string &command_out,
    std::ptrdiff_t line_delta, bool horizontal, int client_width,
    int client_height, float content_top) noexcept {
  return m_workspace_renderer.handle_scroll(
      point_x, point_y, command_out, line_delta, horizontal, client_width,
      client_height, content_top);
}

bool X11ChromeRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command, bool extend_selection) {
  return m_workspace_renderer.handle_editor_input(command, extend_selection);
}

bool X11ChromeRenderer::handle_editor_action(UI::Editor::EditorAction action) {
  return m_workspace_renderer.handle_editor_action(action);
}

std::optional<bool>
X11ChromeRenderer::handle_editor_command(std::string_view command_id) {
  return m_workspace_renderer.handle_editor_command(command_id);
}

std::optional<bool> X11ChromeRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept {
  return m_workspace_renderer.is_editor_command_enabled(command_id);
}

bool X11ChromeRenderer::handle_text_input(std::string_view utf8_text) {
  return m_workspace_renderer.handle_text_input(utf8_text);
}

bool X11ChromeRenderer::handle_terminal_key(Terminal::TerminalInputKey key) {
  return m_workspace_renderer.handle_terminal_key(key);
}

bool X11ChromeRenderer::handle_terminal_control(char letter) {
  return m_workspace_renderer.handle_terminal_control(letter);
}

bool X11ChromeRenderer::handle_terminal_scroll(
    float point_x,
    float point_y,
    std::ptrdiff_t line_delta,
    bool horizontal,
    int client_width,
    int client_height,
    float content_top) noexcept {
  return m_workspace_renderer.handle_terminal_scroll(
      point_x, point_y, line_delta, horizontal, client_width, client_height, content_top);
}

bool X11ChromeRenderer::handle_terminal_scroll(std::ptrdiff_t line_delta,
                                               bool horizontal) noexcept {
  return m_workspace_renderer.handle_terminal_scroll(line_delta, horizontal);
}

bool X11ChromeRenderer::handle_tool_sidebar_scroll(std::ptrdiff_t line_delta,
                                                   int client_width,
                                                   int client_height,
                                                   float content_top) noexcept {
  return m_workspace_renderer.handle_tool_sidebar_scroll(
      line_delta, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_editor_focused() const noexcept {
  return m_workspace_renderer.is_editor_focused();
}

bool X11ChromeRenderer::is_terminal_focused() const noexcept {
  return m_workspace_renderer.is_terminal_focused();
}

bool X11ChromeRenderer::is_activity_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_activity_bar_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_tab_bar_area_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_tab_bar_area_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_tab_bar_point(float point_x, float point_y,
                                         int client_width, int client_height,
                                         float content_top) const noexcept {
  return m_workspace_renderer.is_tab_bar_point(point_x, point_y, client_width,
                                               client_height, content_top);
}

bool X11ChromeRenderer::is_editor_point(float point_x, float point_y,
                                        int client_width, int client_height,
                                        float content_top) const noexcept {
  return m_workspace_renderer.is_editor_point(point_x, point_y, client_width,
                                              client_height, content_top);
}

bool X11ChromeRenderer::is_scrollbar_point(float point_x, float point_y,
                                           int client_width, int client_height,
                                           float content_top) const noexcept {
  return m_workspace_renderer.is_scrollbar_point(point_x, point_y, client_width,
                                                 client_height, content_top);
}

bool X11ChromeRenderer::is_minimap_point(float point_x, float point_y,
                                         int client_width, int client_height,
                                         float content_top) const noexcept {
  return m_workspace_renderer.is_minimap_point(point_x, point_y, client_width,
                                               client_height, content_top);
}

bool X11ChromeRenderer::is_fold_margin_point(float point_x, float point_y,
                                             int client_width,
                                             int client_height,
                                             float content_top) const noexcept {
  return m_workspace_renderer.is_fold_margin_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_point(float point_x, float point_y,
                                          int client_width, int client_height,
                                          float content_top) const noexcept {
  return m_workspace_renderer.is_terminal_point(point_x, point_y, client_width,
                                                client_height, content_top);
}

bool X11ChromeRenderer::is_tool_sidebar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_tool_sidebar_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_terminal_resize_handle_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_terminal_resizing() const noexcept {
  return m_workspace_renderer.is_terminal_resizing();
}

bool X11ChromeRenderer::is_editor_interactive_point(
    float point_x, float point_y) const noexcept {
  return m_workspace_renderer.is_editor_interactive_point(point_x, point_y);
}

bool X11ChromeRenderer::is_terminal_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_terminal_interactive_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_sidebar_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_sidebar_resize_handle_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_sidebar_resizing() const noexcept {
  return m_workspace_renderer.is_sidebar_resizing();
}

bool X11ChromeRenderer::is_shader_panel_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_shader_panel_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_shader_splitter_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  return m_workspace_renderer.is_shader_splitter_point(
      point_x, point_y, client_width, client_height, content_top);
}

bool X11ChromeRenderer::is_shader_panel_resizing() const noexcept {
  return m_workspace_renderer.is_shader_panel_resizing();
}

bool X11ChromeRenderer::toggle_shader_panel() noexcept {
  return m_workspace_renderer.toggle_shader_panel();
}

bool X11ChromeRenderer::is_empty_state_button_hovered() const noexcept {
  return m_workspace_renderer.is_empty_state_button_hovered();
}

bool X11ChromeRenderer::tick_animations() noexcept {
  return m_workspace_renderer.tick_animations();
}

void X11ChromeRenderer::render(
    Window window_handle, int client_width, int client_height,
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    const ChromeInteractionState &interaction_state,
    const CommandStateQueryCallback &command_state_query_callback,
    std::optional<UI::Rect> dirty_rect) {
  if (m_display == nullptr || m_graphics_context == nullptr ||
      window_handle == 0 || client_width <= 0 || client_height <= 0) {
    return;
  }

  const unsigned int pixmap_width = static_cast<unsigned int>(client_width);
  const unsigned int pixmap_height = static_cast<unsigned int>(client_height);
  if (m_back_buffer == 0 || m_back_buffer_w != pixmap_width ||
      m_back_buffer_h != pixmap_height) {
    if (m_back_buffer != 0) {
      XFreePixmap(m_display, m_back_buffer);
      m_back_buffer = 0;
    }
    m_back_buffer = XCreatePixmap(
        m_display, window_handle, pixmap_width, pixmap_height,
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));
    m_back_buffer_w = pixmap_width;
    m_back_buffer_h = pixmap_height;
  }
  if (m_back_buffer == 0) {
    return;
  }

  Pixmap back_buffer = m_back_buffer;

  fill_rectangle(back_buffer,
                 UI::Rect{0.0F, 0.0F, static_cast<float>(client_width),
                          static_cast<float>(client_height)},
                 m_colors.window_background);
  fill_rectangle(back_buffer, chrome_layout.titlebar_bounds,
                 m_colors.titlebar_background);

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  for (std::size_t region_index = 0;
       region_index < chrome_layout.visible_menu_count; ++region_index) {
    const UI::Chrome::MenuRegion &region =
        chrome_layout.menu_regions[region_index];
    const bool hovered =
        interaction_state.hovered_menu_index == region.menu_index ||
        interaction_state.open_menu_index == region.menu_index;
    if (hovered) {
      UI::Rect hover_bounds = region.bounds;
      hover_bounds.y += 4.0F * m_dpi_scale;
      hover_bounds.height -= 8.0F * m_dpi_scale;
      fill_rectangle(back_buffer, hover_bounds, m_colors.hover, 4,
                     m_colors.titlebar_background);
    }
    if (region.menu_index < menus.size()) {
      draw_centered_text(back_buffer, menus[region.menu_index].label,
                         region.bounds, m_text_colors.primary);
    }
  }

  if (chrome_layout.has_overflow_menu()) {
    const bool hidden_menu_open = interaction_state.open_menu_index &&
                                  *interaction_state.open_menu_index >=
                                      chrome_layout.first_overflow_menu_index;
    if (interaction_state.overflow_menu_hovered ||
        interaction_state.overflow_menu_open || hidden_menu_open) {
      UI::Rect hover_bounds = chrome_layout.overflow_menu_bounds;
      hover_bounds.x += 2.0F * m_dpi_scale;
      hover_bounds.width -= 4.0F * m_dpi_scale;
      hover_bounds.y += 4.0F * m_dpi_scale;
      hover_bounds.height -= 8.0F * m_dpi_scale;
      fill_rectangle(back_buffer, hover_bounds, m_colors.hover, 4,
                     m_colors.titlebar_background);
    }
    const int line_half_width = std::max(round_to_int(6.0F * m_dpi_scale), 4);
    const int line_gap = std::max(round_to_int(4.0F * m_dpi_scale), 3);
    const int center_x =
        round_to_int(chrome_layout.overflow_menu_bounds.x +
                     chrome_layout.overflow_menu_bounds.width * 0.5F);
    const int center_y =
        round_to_int(chrome_layout.overflow_menu_bounds.y +
                     chrome_layout.overflow_menu_bounds.height * 0.5F);
    XSetForeground(m_display, m_graphics_context, m_colors.text_primary);
    const int line_thickness = std::max(1, round_to_int(m_dpi_scale));
    XSetLineAttributes(m_display, m_graphics_context, line_thickness, LineSolid,
                       CapRound, JoinRound);
    for (int row = -1; row <= 1; ++row) {
      XDrawLine(m_display, back_buffer, m_graphics_context,
                center_x - line_half_width, center_y + row * line_gap,
                center_x + line_half_width, center_y + row * line_gap);
    }
    XSetLineAttributes(m_display, m_graphics_context, 1, LineSolid, CapButt,
                       JoinMiter);
  }

  const float scale = chrome_layout.dpi_scale;
  const float logo_size = 22.0F * scale;
  const UI::Rect logo_bounds{
      chrome_layout.logo_bounds.x +
          (chrome_layout.logo_bounds.width - logo_size) * 0.5F,
      chrome_layout.logo_bounds.y +
          (chrome_layout.logo_bounds.height - logo_size) * 0.5F,
      logo_size,
      logo_size,
  };
  if (!m_workspace_renderer.draw_ico_icon(
          back_buffer, "Assets/icons/zenvra_logo48x48.ico",
          round_to_int(logo_bounds.x + logo_bounds.width * 0.5F),
          round_to_int(logo_bounds.y + logo_bounds.height * 0.5F),
          round_to_int(logo_size), m_titlebar_background_color)) {
    fill_rectangle(back_buffer, logo_bounds, m_colors.accent,
                   static_cast<int>(logo_size * 0.25F));
    draw_centered_text(back_buffer, "Z", logo_bounds, m_text_colors.white);
  }

  static_cast<void>(m_workspace_renderer.tick_animations());
  m_workspace_renderer.render(back_buffer, client_width, client_height,
                              chrome_layout.titlebar_bounds.bottom());

  // Draw titlebar bottom separator border across full width with proper z-index above content
  const int titlebar_bottom_y =
      round_to_int(chrome_layout.titlebar_bounds.bottom()) - 1;
  XSetForeground(m_display, m_graphics_context, m_colors.titlebar_border);
  XDrawLine(m_display, back_buffer, m_graphics_context, 0, titlebar_bottom_y,
            client_width, titlebar_bottom_y);

  draw_window_control(back_buffer, chrome_layout.minimize_bounds,
                      UI::Chrome::WindowControl::Minimize, interaction_state);
  draw_window_control(back_buffer, chrome_layout.maximize_bounds,
                      UI::Chrome::WindowControl::MaximizeRestore,
                      interaction_state);
  draw_window_control(back_buffer, chrome_layout.close_bounds,
                      UI::Chrome::WindowControl::Close, interaction_state);

  auto draw_toolbar_hover = [&](const UI::Rect &bounds) {
    UI::Rect hover_bounds = bounds;
    hover_bounds.y += 4.0F * scale;
    hover_bounds.height -= 8.0F * scale;
    hover_bounds.x += 2.0F * scale;
    hover_bounds.width -= 4.0F * scale;
    fill_rectangle(back_buffer, hover_bounds, m_colors.hover, 4,
                   m_colors.titlebar_background);
  };

  if (!chrome_layout.build_bounds.is_empty()) {
    if (interaction_state.build_button_hovered) {
      draw_toolbar_hover(chrome_layout.build_bounds);
    }
    const int center_x = round_to_int(chrome_layout.build_bounds.x +
                                      chrome_layout.build_bounds.width * 0.5F);
    const int center_y = round_to_int(chrome_layout.build_bounds.y +
                                      chrome_layout.build_bounds.height * 0.5F);
    const int icon_size = std::max(round_to_int(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/build.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.text_primary,
        interaction_state.build_button_hovered ? m_hover_color
                                               : m_titlebar_background_color);
  }

  if (!chrome_layout.run_bounds.is_empty()) {
    if (interaction_state.run_button_hovered) {
      draw_toolbar_hover(chrome_layout.run_bounds);
    }
    const int center_x = round_to_int(chrome_layout.run_bounds.x +
                                      chrome_layout.run_bounds.width * 0.5F);
    const int center_y = round_to_int(chrome_layout.run_bounds.y +
                                      chrome_layout.run_bounds.height * 0.5F);
    const int icon_size = std::max(round_to_int(20.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/play.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.success,
        interaction_state.run_button_hovered ? m_hover_color
                                             : m_titlebar_background_color);
  }

  if (!chrome_layout.debug_bounds.is_empty()) {
    if (interaction_state.debug_button_hovered) {
      draw_toolbar_hover(chrome_layout.debug_bounds);
    }
    const int center_x = round_to_int(chrome_layout.debug_bounds.x +
                                      chrome_layout.debug_bounds.width * 0.5F);
    const int center_y = round_to_int(chrome_layout.debug_bounds.y +
                                      chrome_layout.debug_bounds.height * 0.5F);
    const int icon_size = std::max(round_to_int(18.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/bug.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.warning,
        interaction_state.debug_button_hovered ? m_hover_color
                                               : m_titlebar_background_color);
  }

  if (!chrome_layout.gear_bounds.is_empty()) {
    if (interaction_state.gear_button_hovered) {
      draw_toolbar_hover(chrome_layout.gear_bounds);
    }
    const int center_x = round_to_int(chrome_layout.gear_bounds.x +
                                      chrome_layout.gear_bounds.width * 0.5F);
    const int center_y = round_to_int(chrome_layout.gear_bounds.y +
                                      chrome_layout.gear_bounds.height * 0.5F);
    const int icon_size = std::max(round_to_int(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/gear.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.text_primary,
        interaction_state.gear_button_hovered ? m_hover_color
                                              : m_titlebar_background_color);
  }

  if (!chrome_layout.ellipsis_bounds.is_empty()) {
    if (interaction_state.ellipsis_button_hovered) {
      draw_toolbar_hover(chrome_layout.ellipsis_bounds);
    }
    const int center_x =
        round_to_int(chrome_layout.ellipsis_bounds.x +
                     chrome_layout.ellipsis_bounds.width * 0.5F);
    const int center_y =
        round_to_int(chrome_layout.ellipsis_bounds.y +
                     chrome_layout.ellipsis_bounds.height * 0.5F);
    const int icon_size = std::max(round_to_int(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/ellipsis.svg", center_x, center_y, icon_size,
        m_workspace_renderer.m_palette.text_primary,
        interaction_state.ellipsis_button_hovered
            ? m_hover_color
            : m_titlebar_background_color);
  }

  // Build toolbar: Compiler | Binary | Mode
  if (!chrome_layout.compiler_bounds.is_empty()) {
    if (interaction_state.compiler_button_hovered) {
      draw_toolbar_hover(chrome_layout.compiler_bounds);
    }
    const UI::Rect text_rect{
        chrome_layout.compiler_bounds.x + 12.0F * scale,
        chrome_layout.compiler_bounds.y,
        std::max(0.0F, chrome_layout.compiler_bounds.width - 28.0F * scale),
        chrome_layout.compiler_bounds.height};
    draw_centered_text(back_buffer, "Debug", text_rect, m_text_colors.primary);
    const int chevron_x =
        round_to_int(chrome_layout.compiler_bounds.right() - 14.0F * scale);
    const int chevron_y =
        round_to_int(chrome_layout.compiler_bounds.y +
                     chrome_layout.compiler_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(static_cast<int>(12.0F * scale), 10),
        m_workspace_renderer.m_palette.text_muted,
        interaction_state.compiler_button_hovered
            ? m_hover_color
            : m_titlebar_background_color);
  }

  if (!chrome_layout.platform_bounds.is_empty()) {
    if (interaction_state.platform_button_hovered) {
      draw_toolbar_hover(chrome_layout.platform_bounds);
    }
    const UI::Rect text_rect{
        chrome_layout.platform_bounds.x + 12.0F * scale,
        chrome_layout.platform_bounds.y,
        std::max(0.0F, chrome_layout.platform_bounds.width - 28.0F * scale),
        chrome_layout.platform_bounds.height};
    draw_centered_text(back_buffer, "x64", text_rect, m_text_colors.primary);
    const int chevron_x =
        round_to_int(chrome_layout.platform_bounds.right() - 14.0F * scale);
    const int chevron_y =
        round_to_int(chrome_layout.platform_bounds.y +
                     chrome_layout.platform_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(static_cast<int>(12.0F * scale), 10),
        m_workspace_renderer.m_palette.text_muted,
        interaction_state.platform_button_hovered
            ? m_hover_color
            : m_titlebar_background_color);
  }

  if (!chrome_layout.binary_bounds.is_empty()) {
    if (interaction_state.binary_button_hovered) {
      draw_toolbar_hover(chrome_layout.binary_bounds);
    }
    const int binary_icon_size = std::max(round_to_int(16.0F * scale), 14);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/terminal.svg",
        round_to_int(chrome_layout.binary_bounds.x + 16.0F * scale),
        round_to_int(chrome_layout.binary_bounds.y +
                     chrome_layout.binary_bounds.height * 0.5F),
        binary_icon_size, m_workspace_renderer.m_palette.text_primary,
        interaction_state.binary_button_hovered ? m_hover_color
                                                : m_titlebar_background_color);
    const UI::Rect text_rect{
        chrome_layout.binary_bounds.x + 36.0F * scale,
        chrome_layout.binary_bounds.y,
        std::max(0.0F, chrome_layout.binary_bounds.width - 52.0F * scale),
        chrome_layout.binary_bounds.height};
    draw_centered_text(back_buffer, "untitled", text_rect, m_text_colors.primary);
    const int chevron_x =
        round_to_int(chrome_layout.binary_bounds.right() - 14.0F * scale);
    const int chevron_y =
        round_to_int(chrome_layout.binary_bounds.y +
                     chrome_layout.binary_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(round_to_int(12.0F * scale), 10),
        m_workspace_renderer.m_palette.text_muted,
        interaction_state.binary_button_hovered ? m_hover_color
                                                : m_titlebar_background_color);
  }

  if (!chrome_layout.mode_bounds.is_empty()) {
    if (interaction_state.mode_button_hovered) {
      draw_toolbar_hover(chrome_layout.mode_bounds);
    }
    draw_text(back_buffer, "Debug", chrome_layout.mode_bounds, 8.0F * scale,
              m_text_colors.primary);
    const int chevron_x =
        round_to_int(chrome_layout.mode_bounds.right() - 14.0F * scale);
    const int chevron_y = round_to_int(chrome_layout.mode_bounds.y +
                                       chrome_layout.mode_bounds.height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        back_buffer, "Assets/icons/chevron-down.svg", chevron_x, chevron_y,
        std::max(round_to_int(12.0F * scale), 10),
        m_workspace_renderer.m_palette.text_muted,
        interaction_state.mode_button_hovered ? m_hover_color
                                              : m_titlebar_background_color);
  }

  if (!interaction_state.maximized) {
    XSetForeground(m_display, m_graphics_context, m_colors.titlebar_border);
    XDrawRectangle(m_display, back_buffer, m_graphics_context, 0, 0,
                   pixmap_width - 1, pixmap_height - 1);
  }

  draw_overflow_menu(back_buffer, chrome_layout, interaction_state);
  draw_popup_menu(back_buffer, chrome_layout, interaction_state,
                  command_state_query_callback);

  if (dirty_rect && !dirty_rect->is_empty()) {
    const int src_x = std::clamp(round_to_int(dirty_rect->x), 0,
                                 static_cast<int>(pixmap_width));
    const int src_y = std::clamp(round_to_int(dirty_rect->y), 0,
                                 static_cast<int>(pixmap_height));
    const int src_w = std::clamp(round_to_int(dirty_rect->width), 1,
                                 static_cast<int>(pixmap_width) - src_x);
    const int src_h = std::clamp(round_to_int(dirty_rect->height), 1,
                                 static_cast<int>(pixmap_height) - src_y);
    XCopyArea(m_display, back_buffer, window_handle, m_graphics_context, src_x,
              src_y, static_cast<unsigned int>(src_w),
              static_cast<unsigned int>(src_h), src_x, src_y);
  } else {
    XCopyArea(m_display, back_buffer, window_handle, m_graphics_context, 0, 0,
              pixmap_width, pixmap_height, 0, 0);
  }
  XFlush(m_display);
}

unsigned long
X11ChromeRenderer::allocate_color(const UI::Theme::Color &color) const {
  XColor x_color{};
  x_color.red = static_cast<unsigned short>(color.red * 257U);
  x_color.green = static_cast<unsigned short>(color.green * 257U);
  x_color.blue = static_cast<unsigned short>(color.blue * 257U);
  x_color.flags = DoRed | DoGreen | DoBlue;
  const Colormap colormap = DefaultColormap(m_display, m_screen);
  if (XAllocColor(m_display, colormap, &x_color) == 0) {
    return BlackPixel(m_display, m_screen);
  }
  return x_color.pixel;
}

OverflowMenuGeometry X11ChromeRenderer::calculate_overflow_menu_geometry(
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout) const noexcept {
  OverflowMenuGeometry geometry;
  geometry.first_menu_index = chrome_layout.first_overflow_menu_index;
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (!chrome_layout.has_overflow_menu() ||
      geometry.first_menu_index >= menus.size()) {
    return geometry;
  }

  const float scale = chrome_layout.dpi_scale;
  const float row_height = 28.0F * scale;
  float popup_width = 168.0F * scale;
  for (std::size_t menu_index = geometry.first_menu_index;
       menu_index < menus.size(); ++menu_index) {
    const UI::Components::Menu &menu = menus[menu_index];
    popup_width = std::max(
        popup_width,
        static_cast<float>(menu.label.size()) * 7.0F * scale + 34.0F * scale);
  }
  geometry.item_count = std::min(menus.size() - geometry.first_menu_index,
                                 geometry.item_bounds.size());
  geometry.bounds = {
      chrome_layout.overflow_menu_bounds.x,
      chrome_layout.titlebar_bounds.bottom(),
      popup_width,
      row_height * static_cast<float>(geometry.item_count),
  };
  for (std::size_t index = 0; index < geometry.item_count; ++index) {
    geometry.item_bounds[index] = {
        geometry.bounds.x,
        geometry.bounds.y + row_height * static_cast<float>(index),
        geometry.bounds.width,
        row_height,
    };
  }
  return geometry;
}

PopupMenuGeometry X11ChromeRenderer::calculate_popup_geometry(
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    std::size_t menu_index, bool opened_from_overflow) const noexcept {
  PopupMenuGeometry geometry;
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size()) {
    return geometry;
  }

  const UI::Rect *anchor_bounds = nullptr;
  for (std::size_t index = 0; index < chrome_layout.visible_menu_count;
       ++index) {
    if (chrome_layout.menu_regions[index].menu_index == menu_index) {
      anchor_bounds = &chrome_layout.menu_regions[index].bounds;
      break;
    }
  }
  if (anchor_bounds == nullptr && chrome_layout.has_overflow_menu() &&
      menu_index >= chrome_layout.first_overflow_menu_index &&
      menu_index < UI::Chrome::window_menu_count) {
    anchor_bounds = &chrome_layout.overflow_menu_bounds;
  }
  if (anchor_bounds == nullptr) {
    static constexpr std::size_t compiler_menu_index = 10;
    static constexpr std::size_t platform_menu_index = 11;
    static constexpr std::size_t binary_menu_index = 12;
    static constexpr std::size_t gear_menu_index = 13;
    static constexpr std::size_t ellipsis_menu_index = 14;
    if (menu_index == compiler_menu_index)
      anchor_bounds = &chrome_layout.compiler_bounds;
    else if (menu_index == platform_menu_index)
      anchor_bounds = &chrome_layout.platform_bounds;
    else if (menu_index == binary_menu_index)
      anchor_bounds = &chrome_layout.binary_bounds;
    else if (menu_index == gear_menu_index)
      anchor_bounds = &chrome_layout.gear_bounds;
    else if (menu_index == ellipsis_menu_index)
      anchor_bounds = &chrome_layout.ellipsis_bounds;
  }
  if (anchor_bounds == nullptr) {
    return geometry;
  }

  const UI::Components::Menu &menu = menus[menu_index];
  const float row_height = 28.0F * m_dpi_scale;
  const float separator_height = 9.0F * m_dpi_scale;
  float popup_width = 240.0F * m_dpi_scale;
  AntialiasedFont *font =
      m_popup_font != nullptr ? m_popup_font.get() : m_font.get();
  for (const UI::Components::MenuItem &item : menu.items) {
    if (item.separator) continue;
    float text_width =
        font != nullptr
            ? static_cast<float>(font->getTextWidth(std::string{item.label}))
            : (static_cast<float>(item.label.size()) * 7.5F * m_dpi_scale);
    float item_width = text_width + 48.0F * m_dpi_scale;
    if (!item.shortcut.empty()) {
      float shortcut_width =
          font != nullptr
              ? static_cast<float>(font->getTextWidth(std::string{item.shortcut}))
              : (static_cast<float>(item.shortcut.size()) * 7.5F * m_dpi_scale);
      item_width += shortcut_width + 36.0F * m_dpi_scale;
    }
    popup_width = std::max(popup_width, item_width);
  }
  popup_width = std::clamp(popup_width, 240.0F * m_dpi_scale, 480.0F * m_dpi_scale);

  float current_y = chrome_layout.titlebar_bounds.bottom();
  geometry.bounds.x = anchor_bounds->x;
  geometry.bounds.y = current_y;
  if (opened_from_overflow) {
    const std::size_t overflow_row =
        menu_index - chrome_layout.first_overflow_menu_index;
    const OverflowMenuGeometry root_geometry =
        calculate_overflow_menu_geometry(chrome_layout);
    geometry.bounds.x = root_geometry.bounds.right() + 2.0F * m_dpi_scale;
    geometry.bounds.y += static_cast<float>(overflow_row) * 28.0F * m_dpi_scale;
    current_y = geometry.bounds.y;
  }
  geometry.bounds.width = popup_width;
  geometry.item_count = std::min(menu.items.size(), max_popup_menu_items);

  for (std::size_t item_index = 0; item_index < geometry.item_count;
       ++item_index) {
    const float height =
        menu.items[item_index].separator ? separator_height : row_height;
    geometry.item_bounds[item_index] = {
        geometry.bounds.x,
        current_y,
        geometry.bounds.width,
        height,
    };
    current_y += height;
  }
  geometry.bounds.height = current_y - geometry.bounds.y;
  return geometry;
}

void X11ChromeRenderer::fill_rectangle(
    Drawable drawable, const UI::Rect &rectangle, unsigned long color,
    int radius, std::optional<unsigned long> bg_color) const {
  if (rectangle.is_empty()) {
    return;
  }

  if (radius > 0) {
    unsigned long actual_bg = bg_color.value_or(m_colors.window_background);
    unsigned long opaque_color = (255UL << 24) | (color & 0xFFFFFF);
    unsigned long opaque_bg = (255UL << 24) | (actual_bg & 0xFFFFFF);
    Utility::X11Rounded::X11Rounded::fillRoundedRectAA(
        m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
        round_to_int(rectangle.y), round_to_int(rectangle.width),
        round_to_int(rectangle.height), radius, opaque_color, opaque_bg, true);
  } else {
    XSetForeground(m_display, m_graphics_context, color);
    XFillRectangle(m_display, drawable, m_graphics_context,
                   round_to_int(rectangle.x), round_to_int(rectangle.y),
                   round_to_int(rectangle.width),
                   round_to_int(rectangle.height));
  }
}

void X11ChromeRenderer::draw_rectangle(Drawable drawable,
                                       const UI::Rect &rectangle,
                                       unsigned long color, int radius) const {
  if (rectangle.is_empty()) {
    return;
  }

  XSetForeground(m_display, m_graphics_context, color);
  Utility::X11Rounded::X11Rounded::drawRoundedRect(
      m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
      round_to_int(rectangle.y), round_to_int(rectangle.width),
      round_to_int(rectangle.height), radius);
}

void X11ChromeRenderer::draw_centered_text(Drawable drawable,
                                           std::string_view text,
                                           const UI::Rect &rectangle,
                                           const std::string &color) const {
  if (text.empty() || rectangle.is_empty()) {
    return;
  }

  const std::string utf8_text{text};
  const int text_width = m_font != nullptr ? m_font->getTextWidth(utf8_text)
                                           : static_cast<int>(text.size()) * 8;
  const int font_ascent = m_font != nullptr ? m_font->getAscent() : 8;
  const int font_descent = m_font != nullptr ? m_font->getDescent() : 2;
  const int text_x = round_to_int(
      rectangle.x + (rectangle.width - static_cast<float>(text_width)) * 0.5F);
  const int text_y = round_to_int(
      rectangle.y +
      (rectangle.height - static_cast<float>(font_ascent + font_descent)) *
          0.5F +
      static_cast<float>(font_ascent));

  if (m_font != nullptr) {
    m_font->drawString(drawable, color, text_x, text_y, utf8_text);
  }
}

void X11ChromeRenderer::draw_text(Drawable drawable, std::string_view text,
                                  const UI::Rect &rectangle, float left_padding,
                                  const std::string &color) const {
  if (text.empty() || rectangle.is_empty()) {
    return;
  }

  const int font_ascent = m_font != nullptr ? m_font->getAscent() : 8;
  const int font_descent = m_font != nullptr ? m_font->getDescent() : 2;
  const int text_x = round_to_int(rectangle.x + left_padding);
  const int text_y = round_to_int(
      rectangle.y +
      (rectangle.height - static_cast<float>(font_ascent + font_descent)) *
          0.5F +
      static_cast<float>(font_ascent));

  if (m_font != nullptr) {
    m_font->drawString(drawable, color, text_x, text_y, std::string{text});
  }
}

void X11ChromeRenderer::draw_window_control(
    Drawable drawable, const UI::Rect &bounds,
    UI::Chrome::WindowControl control,
    const ChromeInteractionState &interaction_state) const {
  if (bounds.is_empty() || control == UI::Chrome::WindowControl::NoControl) {
    return;
  }

  const bool pressed = interaction_state.pressed_control == control;
  const bool hovered = interaction_state.hovered_control == control;
  if (pressed || hovered) {
    const unsigned long background =
        control == UI::Chrome::WindowControl::Close
            ? (pressed ? m_colors.pressed : m_colors.close_hover)
            : (pressed ? m_colors.pressed : m_colors.hover);
    fill_rectangle(drawable, bounds, background);
  }

  const bool white_close_glyph =
      control == UI::Chrome::WindowControl::Close && (hovered || pressed);
  const unsigned long icon_color =
      white_close_glyph ? WhitePixel(m_display, m_screen)
                        : (interaction_state.focused ? m_colors.text_primary
                                                     : m_colors.text_secondary);
  const int line_width = std::max(round_to_int(m_dpi_scale), 1);
  const int center_x = round_to_int(bounds.x + bounds.width * 0.5F);
  const int center_y = round_to_int(bounds.y + bounds.height * 0.5F);
  const int half_size = std::max(round_to_int(5.0F * m_dpi_scale), 4);
  const int icon_size = half_size * 2;

  XSetForeground(m_display, m_graphics_context, icon_color);
  XSetLineAttributes(m_display, m_graphics_context, line_width, LineSolid,
                     CapProjecting, JoinMiter);
  if (control == UI::Chrome::WindowControl::Minimize) {
    XDrawLine(m_display, drawable, m_graphics_context, center_x - half_size,
              center_y, center_x + half_size, center_y);
  } else if (control == UI::Chrome::WindowControl::MaximizeRestore) {
    if (interaction_state.maximized) {
      const int offset = std::max(round_to_int(2.0F * m_dpi_scale), 2);
      // Top and right edges of back box
      XDrawLine(m_display, drawable, m_graphics_context,
                center_x - half_size + offset, center_y - half_size,
                center_x + half_size, center_y - half_size);
      XDrawLine(m_display, drawable, m_graphics_context,
                center_x + half_size, center_y - half_size,
                center_x + half_size, center_y + half_size - offset);
      XDrawLine(m_display, drawable, m_graphics_context,
                center_x - half_size + offset, center_y - half_size,
                center_x - half_size + offset, center_y - half_size + offset);

      // Front box
      const unsigned long front_bg =
          (hovered ? m_colors.hover : (pressed ? m_colors.pressed : m_colors.titlebar_background));
      fill_rectangle(
          drawable,
          UI::Rect{static_cast<float>(center_x - half_size),
                   static_cast<float>(center_y - half_size + offset),
                   static_cast<float>(icon_size - offset),
                   static_cast<float>(icon_size - offset)},
          front_bg);
      XSetForeground(m_display, m_graphics_context, icon_color);
      XDrawRectangle(m_display, drawable, m_graphics_context,
                     center_x - half_size, center_y - half_size + offset,
                     static_cast<unsigned int>(icon_size - offset),
                     static_cast<unsigned int>(icon_size - offset));
    } else {
      XDrawRectangle(m_display, drawable, m_graphics_context,
                     center_x - half_size, center_y - half_size,
                     static_cast<unsigned int>(icon_size),
                     static_cast<unsigned int>(icon_size));
    }
  } else if (control == UI::Chrome::WindowControl::Close) {
    XDrawLine(m_display, drawable, m_graphics_context, center_x - half_size,
              center_y - half_size, center_x + half_size, center_y + half_size);
    XDrawLine(m_display, drawable, m_graphics_context, center_x - half_size,
              center_y + half_size, center_x + half_size, center_y - half_size);
  }
  XSetLineAttributes(m_display, m_graphics_context, 1, LineSolid, CapButt,
                     JoinMiter);
}

void X11ChromeRenderer::draw_popup_menu(
    Drawable drawable,
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    const ChromeInteractionState &interaction_state,
    const CommandStateQueryCallback &command_state_query_callback) const {
  if (!interaction_state.open_menu_index || is_popup_open()) {
    return;
  }

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  const std::size_t menu_index = *interaction_state.open_menu_index;
  if (menu_index >= menus.size()) {
    return;
  }

  const PopupMenuGeometry geometry = calculate_popup_geometry(
      chrome_layout, menu_index, interaction_state.overflow_menu_open);
  if (geometry.bounds.is_empty()) {
    return;
  }

  const int popup_radius = std::max(round_to_int(7.0F * m_dpi_scale), 5);
  fill_rectangle(drawable, geometry.bounds, m_colors.popup_background,
                 popup_radius);
  draw_rectangle(drawable, geometry.bounds, m_colors.popup_border,
                 popup_radius);

  const UI::Components::Menu &menu = menus[menu_index];
  for (std::size_t item_index = 0; item_index < geometry.item_count;
       ++item_index) {
    const UI::Components::MenuItem &item = menu.items[item_index];
    const UI::Rect &item_bounds = geometry.item_bounds[item_index];
    if (item.separator) {
      const float line_y = item_bounds.y + item_bounds.height * 0.5F;
      fill_rectangle(drawable,
                     UI::Rect{
                         item_bounds.x + 8.0F * m_dpi_scale,
                         line_y,
                         item_bounds.width - 16.0F * m_dpi_scale,
                         1.0F,
                     },
                     m_colors.popup_border);
      continue;
    }

    CommandPresentationState state =
        item.command_id.empty()
            ? CommandPresentationState{}
            : (command_state_query_callback
                   ? command_state_query_callback(item.command_id)
                   : CommandPresentationState{true, false});
    if (const std::optional<bool> editor_enabled =
            m_workspace_renderer.is_editor_command_enabled(item.command_id)) {
      state.enabled = *editor_enabled;
    }
    const bool is_hovered =
        interaction_state.hovered_popup_item_index == item_index &&
        state.enabled;
    if (is_hovered) {
      UI::Rect hover_bounds = item_bounds;
      hover_bounds.x += 3.0F * m_dpi_scale;
      hover_bounds.width -= 6.0F * m_dpi_scale;
      hover_bounds.y += 1.0F * m_dpi_scale;
      hover_bounds.height -= 2.0F * m_dpi_scale;
      fill_rectangle(drawable, hover_bounds, m_colors.accent,
                     std::max(round_to_int(4.0F * m_dpi_scale), 3));
    }

    std::string text_color = m_text_colors.secondary;
    if (state.enabled) {
      text_color = is_hovered ? m_text_colors.white : m_text_colors.primary;
    }

    // Debug log for hover state
    static int draw_counter = 0;
    if (is_hovered && ++draw_counter % 30 == 0) {
      std::clog << "[DBG] DRAW HOVER: menu=" << menu_index
                << " item=" << item_index << " id=" << item.command_id
                << " enabled=" << state.enabled << " h_idx="
                << interaction_state.hovered_popup_item_index.value_or(999)
                << "\n";
    } else if (!is_hovered &&
               interaction_state.hovered_popup_item_index == item_index &&
               ++draw_counter % 30 == 0) {
      std::clog << "[DBG] DRAW HOVER FAILED: menu=" << menu_index
                << " item=" << item_index << " id=" << item.command_id
                << " enabled=" << state.enabled << " editor_enabled="
                << (m_workspace_renderer
                            .is_editor_command_enabled(item.command_id)
                            .has_value()
                        ? std::to_string(
                              *m_workspace_renderer.is_editor_command_enabled(
                                  item.command_id))
                        : "none")
                << " cb_enabled="
                << (command_state_query_callback
                        ? command_state_query_callback(item.command_id).enabled
                        : -1)
                << "\n";
    }

    draw_text(drawable, item.label, item_bounds, 26.0F * m_dpi_scale,
              text_color);

    if (!item.shortcut.empty()) {
      AntialiasedFont *font =
          m_popup_font != nullptr ? m_popup_font.get() : m_font.get();
      if (font != nullptr) {
        const std::string shortcut_color = !state.enabled
            ? m_text_colors.secondary
            : (is_hovered ? m_text_colors.white : m_text_colors.secondary);
        const int shortcut_w = font->getTextWidth(std::string{item.shortcut});
        const int padding_right = round_to_int(16.0F * m_dpi_scale);
        const int text_x = round_to_int(item_bounds.x + item_bounds.width) -
                           padding_right - shortcut_w;
        const int text_y = round_to_int(
            item_bounds.y + item_bounds.height * 0.5F + font->getAscent() * 0.5F - 2.0F * m_dpi_scale);
        font->drawString(drawable, shortcut_color, text_x, text_y,
                         std::string{item.shortcut});
      }
    }
    if (state.checked) {
      XSetForeground(m_display, m_graphics_context,
                     is_hovered ? WhitePixel(m_display, m_screen)
                                : m_colors.text_primary);
      const int check_x = round_to_int(item_bounds.x + 11.0F * m_dpi_scale);
      const int check_y =
          round_to_int(item_bounds.y + item_bounds.height * 0.5F);
      XDrawLine(m_display, drawable, m_graphics_context, check_x, check_y,
                check_x + 3, check_y + 3);
      XDrawLine(m_display, drawable, m_graphics_context, check_x + 3,
                check_y + 3, check_x + 8, check_y - 3);
    }
  }
}

void X11ChromeRenderer::draw_overflow_menu(
    Drawable drawable,
    const UI::Chrome::WindowChromeLayoutResult &chrome_layout,
    const ChromeInteractionState &interaction_state) const {
  if (!interaction_state.overflow_menu_open) {
    return;
  }

  const OverflowMenuGeometry geometry =
      calculate_overflow_menu_geometry(chrome_layout);
  if (geometry.bounds.is_empty()) {
    return;
  }

  const int popup_radius = std::max(round_to_int(7.0F * m_dpi_scale), 5);
  fill_rectangle(drawable, geometry.bounds, m_colors.popup_background,
                 popup_radius);
  draw_rectangle(drawable, geometry.bounds, m_colors.popup_border,
                 popup_radius);
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  for (std::size_t item_index = 0; item_index < geometry.item_count;
       ++item_index) {
    const std::size_t menu_index = geometry.first_menu_index + item_index;
    if (menu_index >= menus.size()) {
      break;
    }
    const bool is_hovered =
        interaction_state.hovered_overflow_menu_index == menu_index ||
        interaction_state.open_menu_index == menu_index;
    if (is_hovered) {
      UI::Rect hover_bounds = geometry.item_bounds[item_index];
      hover_bounds.x += 4.0F * m_dpi_scale;
      hover_bounds.width -= 8.0F * m_dpi_scale;
      hover_bounds.y += 2.0F * m_dpi_scale;
      hover_bounds.height -= 4.0F * m_dpi_scale;
      fill_rectangle(drawable, hover_bounds, m_colors.accent,
                     std::max(round_to_int(4.0F * m_dpi_scale), 3),
                     m_colors.popup_background);
    }
    draw_text(drawable, menus[menu_index].label,
              geometry.item_bounds[item_index], 12.0F * m_dpi_scale,
              is_hovered ? m_text_colors.white : m_text_colors.primary);

    const int chevron_x = round_to_int(
        geometry.item_bounds[item_index].right() - 14.0F * m_dpi_scale);
    const int chevron_y =
        round_to_int(geometry.item_bounds[item_index].y +
                     geometry.item_bounds[item_index].height * 0.5F);
    m_workspace_renderer.draw_svg_icon(
        drawable, "Assets/icons/chevron-right.svg", chevron_x, chevron_y,
        std::max(round_to_int(12.0F * m_dpi_scale), 10),
        m_workspace_renderer.m_palette.text_muted,
        is_hovered ? UI::Theme::StudioTheme::zenvra_dark().accent
                   : UI::Theme::StudioTheme::zenvra_dark().panel_background);
  }
}

bool X11ChromeRenderer::open_popup(Window parent_window,
                                   const UI::Rect &anchor_bounds,
                                   std::span<const PopupMenuItem> items,
                                   bool select_first_item, bool side_popup) {
  close_popup();
  if (m_display == nullptr || parent_window == 0 || items.empty()) {
    return false;
  }

  m_popup.items.assign(items.begin(), items.end());

  const float row_height = 28.0F * m_dpi_scale;
  const float separator_height = 9.0F * m_dpi_scale;
  const float horizontal_padding = 6.0F * m_dpi_scale;
  int widest = 0;
  float content_height = 0.0F;
  AntialiasedFont *font = m_popup_font ? m_popup_font.get() : m_font.get();
  for (const PopupMenuItem &item : m_popup.items) {
    if (item.separator) {
      content_height += separator_height;
      continue;
    }
    const int text_width = font != nullptr
                               ? font->getTextWidth(item.text)
                               : static_cast<int>(item.text.size()) * 8;
    int item_total_width = text_width + round_to_int(48.0F * m_dpi_scale);
    if (!item.shortcut.empty()) {
      const int shortcut_width = font != nullptr
                                     ? font->getTextWidth(item.shortcut)
                                     : static_cast<int>(item.shortcut.size()) * 8;
      item_total_width += shortcut_width + round_to_int(36.0F * m_dpi_scale);
    }
    widest = std::max(widest, item_total_width);
    content_height += row_height;
  }

  m_popup.width = std::clamp(widest,
                             round_to_int(240.0F * m_dpi_scale),
                             round_to_int(480.0F * m_dpi_scale));
  m_popup.height = round_to_int(content_height + 2.0F * horizontal_padding);

  Window child = 0;
  int target_x = side_popup
                     ? round_to_int(anchor_bounds.right() + 2.0F * m_dpi_scale)
                     : round_to_int(anchor_bounds.x);
  const int target_y =
      side_popup ? round_to_int(anchor_bounds.y)
                 : round_to_int(anchor_bounds.y + anchor_bounds.height);

  if (!side_popup) {
    Window root_ret;
    int x_ret, y_ret;
    unsigned int parent_width, parent_height, border_width, depth;
    if (XGetGeometry(m_display, parent_window, &root_ret, &x_ret, &y_ret,
                     &parent_width, &parent_height, &border_width, &depth)) {
      if (target_x + m_popup.width > static_cast<int>(parent_width)) {
        target_x = round_to_int(anchor_bounds.right()) - m_popup.width;
      }
    }
  }

  if (!XTranslateCoordinates(m_display, parent_window,
                             RootWindow(m_display, m_screen), target_x,
                             target_y, &m_popup.x, &m_popup.y, &child)) {
    return false;
  }

  const int screen_width = DisplayWidth(m_display, m_screen);
  const int screen_height = DisplayHeight(m_display, m_screen);
  if (!side_popup && m_popup.y + m_popup.height > screen_height) {
    const int anchor_top_root_y =
        m_popup.y - round_to_int(anchor_bounds.height);
    if (anchor_top_root_y >= m_popup.height && anchor_top_root_y >= 0) {
      m_popup.y = anchor_top_root_y - m_popup.height;
    }
  }
  m_popup.x =
      std::clamp(m_popup.x, 0, std::max(screen_width - m_popup.width, 0));
  m_popup.y =
      std::clamp(m_popup.y, 0, std::max(screen_height - m_popup.height, 0));

  XSetWindowAttributes attributes{};
  attributes.override_redirect = True;
  attributes.background_pixel = 0; // Transparent
  attributes.save_under = True;
  attributes.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                          PointerMotionMask | LeaveWindowMask | FocusChangeMask;

  unsigned long value_mask =
      CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask;
  if (m_popup_colormap != 0) {
    attributes.colormap = m_popup_colormap;
    attributes.border_pixel = 0;
    value_mask |= CWColormap | CWBorderPixel;
  }

  m_popup.window =
      XCreateWindow(m_display, RootWindow(m_display, m_screen), m_popup.x,
                    m_popup.y, static_cast<unsigned int>(m_popup.width),
                    static_cast<unsigned int>(m_popup.height), 0, m_popup_depth,
                    InputOutput, m_popup_visual, value_mask, &attributes);
  if (m_popup.window == 0) {
    close_popup();
    return false;
  }

  XMapRaised(m_display, m_popup.window);
  XGrabPointer(m_display, m_popup.window, True,
               ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
               GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
  m_popup.open = true;
  m_popup.hovered = select_first_item ? 0 : -1;
  paint_popup();
  XFlush(m_display);
  return true;
}

void X11ChromeRenderer::close_popup() noexcept {
  if (m_display != nullptr && m_popup.open) {
    XUngrabPointer(m_display, CurrentTime);
  }
  destroy_popup_window();
  m_popup = PopupWindowState{};
}

bool X11ChromeRenderer::is_popup_open() const noexcept { return m_popup.open; }

Window X11ChromeRenderer::popup_window() const noexcept {
  return m_popup.window;
}

void X11ChromeRenderer::destroy_popup_window() noexcept {
  if (m_display != nullptr && m_popup_back_buffer != 0) {
    XFreePixmap(m_display, m_popup_back_buffer);
    m_popup_back_buffer = 0;
    m_popup_back_buffer_w = 0;
    m_popup_back_buffer_h = 0;
  }
  if (m_display != nullptr && m_popup.window != 0) {
    XDestroyWindow(m_display, m_popup.window);
    XFlush(m_display);
  }
}

int X11ChromeRenderer::popup_item_index_at(int local_x,
                                           int local_y) const noexcept {
  if (local_x < 0 || local_x >= m_popup.width || local_y < 0 ||
      local_y >= m_popup.height) {
    return -1;
  }
  const float row_height = 28.0F * m_dpi_scale;
  const float separator_height = 9.0F * m_dpi_scale;
  const float horizontal_padding = 6.0F * m_dpi_scale;
  float current_y = horizontal_padding;
  for (std::size_t index = 0; index < m_popup.items.size(); ++index) {
    const float height =
        m_popup.items[index].separator ? separator_height : row_height;
    if (static_cast<float>(local_y) >= current_y &&
        static_cast<float>(local_y) < current_y + height) {
      return static_cast<int>(index);
    }
    current_y += height;
  }
  return -1;
}

void X11ChromeRenderer::paint_popup() {
  if (!m_popup.open || m_popup.window == 0 || m_display == nullptr ||
      m_popup.width <= 0 || m_popup.height <= 0) {
    return;
  }

  const unsigned int popup_width = static_cast<unsigned int>(m_popup.width);
  const unsigned int popup_height = static_cast<unsigned int>(m_popup.height);
  if (m_popup_back_buffer == 0 || m_popup_back_buffer_w != popup_width ||
      m_popup_back_buffer_h != popup_height) {
    if (m_popup_back_buffer != 0) {
      XFreePixmap(m_display, m_popup_back_buffer);
      m_popup_back_buffer = 0;
    }
    m_popup_back_buffer = XCreatePixmap(m_display, m_popup.window, popup_width,
                                        popup_height, m_popup_depth);
    m_popup_back_buffer_w = popup_width;
    m_popup_back_buffer_h = popup_height;
  }
  if (m_popup_back_buffer == 0) {
    return;
  }

  Pixmap buffer = m_popup_back_buffer;
  const int radius = std::max(round_to_int(7.0F * m_dpi_scale), 5);

  GC gc =
      m_popup_graphics_context ? m_popup_graphics_context : m_graphics_context;

  // Clear background to transparent
  XSetForeground(m_display, gc, 0);
  XFillRectangle(m_display, buffer, gc, 0, 0, m_popup.width, m_popup.height);

  // Pack ARGB manually if using 32-bit depth
  unsigned long bg_color = m_colors.popup_background;
  unsigned long border_color = m_colors.popup_border;
  if (m_popup_depth == 32) {
    bg_color = (255UL << 24) | (bg_color & 0xFFFFFF);
    border_color = (255UL << 24) | (border_color & 0xFFFFFF);
  }

  // Draw rounded background and border
  Utility::X11Rounded::X11Rounded::fillRoundedRectAA(
      m_display, buffer, gc, 0, 0, m_popup.width, m_popup.height, radius,
      bg_color, 0, m_popup_depth == 32);
  XSetForeground(m_display, gc, border_color);
  Utility::X11Rounded::X11Rounded::drawRoundedRect(
      m_display, buffer, gc, 0, 0, m_popup.width, m_popup.height, radius);

  const float row_height = 28.0F * m_dpi_scale;
  const float separator_height = 9.0F * m_dpi_scale;
  const float horizontal_padding = 6.0F * m_dpi_scale;
  float current_y = horizontal_padding;
  for (std::size_t index = 0; index < m_popup.items.size(); ++index) {
    const PopupMenuItem &item = m_popup.items[index];
    const float height = item.separator ? separator_height : row_height;
    const UI::Rect item_bounds{0.0F, current_y,
                               static_cast<float>(m_popup.width), height};
    if (item.separator) {
      unsigned long sep_color = m_colors.popup_border;
      if (m_popup_depth == 32) {
        sep_color = (255UL << 24) | (sep_color & 0xFFFFFF);
      }
      XSetForeground(m_display, gc, sep_color);
      Utility::X11Rounded::X11Rounded::fillRoundedRect(
          m_display, buffer, gc, round_to_int(8.0F * m_dpi_scale),
          round_to_int(current_y + separator_height * 0.5F),
          round_to_int(static_cast<float>(m_popup.width) - 16.0F * m_dpi_scale),
          round_to_int(1.0F), 0);
      current_y += separator_height;
      continue;
    }

    const bool is_hovered =
        static_cast<int>(index) == m_popup.hovered && item.enabled;
    if (is_hovered) {
      UI::Rect hover_bounds = item_bounds;
      hover_bounds.x += 4.0F * m_dpi_scale;
      hover_bounds.width -= 8.0F * m_dpi_scale;
      hover_bounds.y += 2.0F * m_dpi_scale;
      hover_bounds.height -= 4.0F * m_dpi_scale;

      unsigned long hover_bg = m_colors.accent;
      if (m_popup_depth == 32) {
        hover_bg = (255UL << 24) | (hover_bg & 0xFFFFFF);
      }
      Utility::X11Rounded::X11Rounded::fillRoundedRectAA(
          m_display, buffer, gc, round_to_int(hover_bounds.x),
          round_to_int(hover_bounds.y), round_to_int(hover_bounds.width),
          round_to_int(hover_bounds.height),
          std::max(round_to_int(4.0F * m_dpi_scale), 3), hover_bg, bg_color,
          m_popup_depth == 32);
    }

    std::string text_color = m_text_colors.secondary;
    if (item.enabled) {
      text_color = is_hovered ? m_text_colors.white : m_text_colors.primary;
    }
    AntialiasedFont *font = m_popup_font ? m_popup_font.get() : m_font.get();
    if (font) {
      font->drawString(
          buffer, text_color, round_to_int(item_bounds.x + 26.0F * m_dpi_scale),
          round_to_int(item_bounds.y + item_bounds.height * 0.5F +
                       font->getAscent() * 0.5F - 2.0F * m_dpi_scale),
          item.text);

      if (!item.shortcut.empty()) {
        const std::string shortcut_color = !item.enabled
            ? m_text_colors.secondary
            : (is_hovered ? m_text_colors.white : m_text_colors.secondary);
        const int shortcut_w = font->getTextWidth(item.shortcut);
        const int shortcut_x = round_to_int(item_bounds.x + item_bounds.width - 16.0F * m_dpi_scale) - shortcut_w;
        const int shortcut_y = round_to_int(
            item_bounds.y + item_bounds.height * 0.5F + font->getAscent() * 0.5F - 2.0F * m_dpi_scale);
        font->drawString(buffer, shortcut_color, shortcut_x, shortcut_y, item.shortcut);
      }
    }

    if (item.checked) {
      unsigned long check_color =
          is_hovered ? WhitePixel(m_display, m_screen) : m_colors.text_primary;
      if (m_popup_depth == 32) {
        check_color = (255UL << 24) | (check_color & 0xFFFFFF);
      }
      XSetForeground(m_display, gc, check_color);
      const int check_x = round_to_int(item_bounds.x + 11.0F * m_dpi_scale);
      const int check_y =
          round_to_int(item_bounds.y + item_bounds.height * 0.5F);
      XDrawLine(m_display, buffer, gc, check_x, check_y, check_x + 3,
                check_y + 3);
      XDrawLine(m_display, buffer, gc, check_x + 3, check_y + 3, check_x + 8,
                check_y - 3);
    }
    current_y += row_height;
  }

  XCopyArea(m_display, buffer, m_popup.window, gc, 0, 0,
            static_cast<unsigned int>(m_popup.width),
            static_cast<unsigned int>(m_popup.height), 0, 0);
  XFlush(m_display);
}

std::optional<std::string> X11ChromeRenderer::take_popup_command() noexcept {
  if (m_popup.selected_command.empty()) {
    return std::nullopt;
  }
  std::string command = std::move(m_popup.selected_command);
  m_popup.selected_command.clear();
  return command;
}

void X11ChromeRenderer::select_popup_item(int index) {
  if (index < 0 || index >= static_cast<int>(m_popup.items.size()) ||
      m_popup.items[index].separator || !m_popup.items[index].enabled) {
    return;
  }
  std::string command = m_popup.items[index].command_id;
  close_popup();
  m_popup.selected_command = std::move(command);
}

void X11ChromeRenderer::move_popup_selection(int direction) {
  if (static_cast<int>(m_popup.items.size()) == 0 || direction == 0) {
    return;
  }
  const int item_count = static_cast<int>(m_popup.items.size());
  int candidate = m_popup.hovered;
  for (int attempt = 0; attempt < item_count; ++attempt) {
    candidate += direction > 0 ? 1 : -1;
    if (candidate < 0) {
      candidate = item_count - 1;
    } else if (candidate >= item_count) {
      candidate = 0;
    }
    if (!m_popup.items[candidate].separator &&
        m_popup.items[candidate].enabled) {
      m_popup.hovered = candidate;
      paint_popup();
      return;
    }
  }
  m_popup.hovered = -1;
  paint_popup();
}

void X11ChromeRenderer::activate_popup_selection() {
  select_popup_item(m_popup.hovered);
}

bool X11ChromeRenderer::handle_popup_event(const XEvent &event) {
  if (!m_popup.open || m_popup.window == 0) {
    return false;
  }
  if (event.xany.window != m_popup.window && event.type != FocusOut) {
    return false;
  }

  switch (event.type) {
  case Expose:
    if (event.xexpose.count == 0) {
      paint_popup();
    }
    break;

  case MotionNotify: {
    const int item_index = popup_item_index_at(
        event.xmotion.x_root - m_popup.x, event.xmotion.y_root - m_popup.y);
    if (item_index >= 0 && (m_popup.items[item_index].separator ||
                            !m_popup.items[item_index].enabled)) {
      break;
    }
    if (item_index != m_popup.hovered) {
      m_popup.hovered = item_index;
      paint_popup();
    }
    break;
  }

  case ButtonPress: {
    const int item = popup_item_index_at(event.xbutton.x_root - m_popup.x,
                                         event.xbutton.y_root - m_popup.y);
    if (item < 0) {
      close_popup();
      return true;
    }
    if (event.xbutton.button == Button1) {
      m_popup.pressed = true;
    }
    break;
  }

  case ButtonRelease:
    if (event.xbutton.button == Button1) {
      m_popup.pressed = false;
      const int item = popup_item_index_at(event.xbutton.x_root - m_popup.x,
                                           event.xbutton.y_root - m_popup.y);
      if (item >= 0) {
        select_popup_item(item);
      } else {
        close_popup();
      }
    }
    break;

  case LeaveNotify:
    if (m_popup.pressed) {
      m_popup.pressed = false;
      close_popup();
    }
    break;

  case FocusOut:
    close_popup();
    break;

  default:
    break;
  }
  return true;
}

} // namespace Zenvra::Platform::X11::Components
