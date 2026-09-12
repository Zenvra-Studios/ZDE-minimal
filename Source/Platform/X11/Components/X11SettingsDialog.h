#pragma once

#include "UI/Geometry.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/StudioTheme.h"
#include <X11/Xlib.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class AntialiasedFont;

namespace Zenvra::Platform::X11::Components {

class X11SettingsDialog {
public:
  X11SettingsDialog();
  ~X11SettingsDialog();

  X11SettingsDialog(const X11SettingsDialog &) = delete;
  X11SettingsDialog &operator=(const X11SettingsDialog &) = delete;

  [[nodiscard]] bool
  initialize(Display *display, int screen, float dpi_scale,
             const std::filesystem::path &icon_asset_root = {});

  void open(Window parent_window);
  void close();
  void shutdown();
  void set_on_close_callback(std::function<void()> callback) {
    m_on_close_callback = std::move(callback);
  }
  [[nodiscard]] bool is_open() const noexcept { return m_open && m_window != 0; }
  [[nodiscard]] Window window() const noexcept { return m_window; }

  void apply_theme(const UI::Theme::StudioTheme &theme);

  bool handle_event(const XEvent &event);
  void render();
  [[nodiscard]] bool tick_animations() noexcept;

  [[nodiscard]] UI::Settings::SettingsWindow &get_engine() noexcept {
    return m_engine;
  }
  [[nodiscard]] const UI::Settings::SettingsWindow &
  get_engine() const noexcept {
    return m_engine;
  }

private:
  void draw_icon(Drawable drawable, const std::string &path, int x, int y,
                 int size, const UI::Theme::Color &tint,
                 const UI::Theme::Color &bg);
  void fill_rect(Drawable drawable, const UI::Rect &rect, unsigned long pixel);
  void draw_rect(Drawable drawable, const UI::Rect &rect, unsigned long pixel);
  void fill_rounded_rect(Drawable drawable, const UI::Rect &rect,
                         unsigned long fill_px, float radius,
                         unsigned long bg_px);
  void draw_rounded_rect(Drawable drawable, const UI::Rect &rect,
                         unsigned long border_px, float radius);
  void draw_line_segment(Drawable drawable, int x1, int y1, int x2, int y2,
                         unsigned long pixel);
  void set_clip(const UI::Rect &rect);
  void reset_clip();

  [[nodiscard]] unsigned long
  color_to_pixel(const UI::Theme::Color &color) const;
  [[nodiscard]] float get_text_width(AntialiasedFont &font,
                                     std::string_view text) const;
  void draw_text(Drawable drawable, AntialiasedFont &font,
                 std::string_view text, float x, float y,
                 const std::string &hex_color);

  Display *m_display = nullptr;
  int m_screen = 0;
  float m_dpi_scale = 1.0F;
  std::filesystem::path m_icon_asset_root;

  Window m_parent_window = 0;
  Window m_window = 0;
  Pixmap m_back_buffer = 0;
  GC m_gc = nullptr;

  std::unique_ptr<AntialiasedFont> m_title_font;
  std::unique_ptr<AntialiasedFont> m_large_font;
  std::unique_ptr<AntialiasedFont> m_ui_font;
  std::unique_ptr<AntialiasedFont> m_small_font;

  int m_width = 920;
  int m_height = 620;
  int m_win_x = 0;
  int m_win_y = 0;

  bool m_open = false;
  bool m_close_hovered = false;

  bool m_dragging_titlebar = false;
  int m_drag_start_root_x = 0;
  int m_drag_start_root_y = 0;
  int m_drag_start_win_x = 0;
  int m_drag_start_win_y = 0;

  UI::Settings::SettingsWindow m_engine;
  UI::Theme::StudioTheme m_theme = UI::Theme::StudioTheme::zenvra_dark();

  std::function<void()> m_on_close_callback;
  mutable std::unordered_map<std::string, XImage *> m_svg_cache;
};

} // namespace Zenvra::Platform::X11::Components
