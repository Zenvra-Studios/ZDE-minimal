#include "Platform/X11/Components/X11SettingsDialog.h"
#include "Settings/SettingsService.h"
#include "Utility/Fonts.h"
#include "Utility/X11Rounded.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <lunasvg.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

namespace Zenvra::Platform::X11::Components {

static inline int round_to_int(float val) noexcept {
  return static_cast<int>(std::round(val));
}

static std::string to_hex_color(const UI::Theme::Color &c) {
  char buf[10]{};
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.red, c.green, c.blue);
  return std::string{buf};
}

X11SettingsDialog::X11SettingsDialog() = default;

X11SettingsDialog::~X11SettingsDialog() {
  shutdown();
}

bool X11SettingsDialog::initialize(
    Display *display, int screen, float dpi_scale,
    const std::filesystem::path &icon_asset_root) {
  m_display = display;
  m_screen = screen;
  m_dpi_scale = std::max(dpi_scale, 1.0F);
  m_icon_asset_root = icon_asset_root;

  const int base_title_size =
      std::max(12, static_cast<int>(14.0F * m_dpi_scale));
  const int base_large_size =
      std::max(13, static_cast<int>(15.0F * m_dpi_scale));
  const int base_ui_size =
      std::max(10, static_cast<int>(12.0F * m_dpi_scale));
  const int base_small_size =
      std::max(9, static_cast<int>(11.0F * m_dpi_scale));

  char pattern[256]{};
  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, "
                "sans-serif:pixelsize=%d:weight=bold:antialias=true:hinting="
                "true:hintstyle=hintslight",
                base_title_size);
  m_title_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, "
                "sans-serif:pixelsize=%d:weight=bold:antialias=true:hinting="
                "true:hintstyle=hintslight",
                base_large_size);
  m_large_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, "
                "sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle="
                "hintslight",
                base_ui_size);
  m_ui_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  std::snprintf(pattern, sizeof(pattern),
                "Open Sans, Adwaita Sans, Inter, Cantarell, "
                "sans-serif:pixelsize=%d:antialias=true:hinting=true:hintstyle="
                "hintslight",
                base_small_size);
  m_small_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, pattern);

  m_engine.set_theme(m_theme);
  return true;
}

void X11SettingsDialog::open(Window parent_window) {
  if (m_open && m_window != 0) {
    XRaiseWindow(m_display, m_window);
    XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
    render();
    return;
  }
  close();

  m_parent_window = parent_window;
  const float scale = m_dpi_scale;
  m_width = static_cast<int>(920.0F * scale);
  m_height = static_cast<int>(620.0F * scale);

  XWindowAttributes parent_attrs{};
  XGetWindowAttributes(m_display, parent_window, &parent_attrs);

  int root_x = 0;
  int root_y = 0;
  Window child = 0;
  XTranslateCoordinates(m_display, parent_window,
                        RootWindow(m_display, m_screen), 0, 0, &root_x, &root_y,
                        &child);

  m_win_x = root_x + (parent_attrs.width - m_width) / 2;
  m_win_y = root_y + (parent_attrs.height - m_height) / 2;

  const int screen_w = DisplayWidth(m_display, m_screen);
  const int screen_h = DisplayHeight(m_display, m_screen);
  m_win_x = std::clamp(m_win_x, 0, std::max(screen_w - m_width, 0));
  m_win_y = std::clamp(m_win_y, 0, std::max(screen_h - m_height, 0));

  XSetWindowAttributes attrs{};
  attrs.override_redirect = True;
  attrs.background_pixel = color_to_pixel(m_theme.panel_background);
  attrs.save_under = True;
  attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | KeyPressMask | EnterWindowMask |
                     LeaveWindowMask | FocusChangeMask;

  m_window = XCreateWindow(
      m_display, RootWindow(m_display, m_screen), m_win_x, m_win_y,
      static_cast<unsigned int>(m_width), static_cast<unsigned int>(m_height),
      0, DefaultDepth(m_display, m_screen), InputOutput,
      DefaultVisual(m_display, m_screen),
      CWOverrideRedirect | CWBackPixel | CWSaveUnder | CWEventMask, &attrs);

  if (parent_window != 0) {
    XSetTransientForHint(m_display, m_window, parent_window);
  }

  struct MotifHints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long inputMode;
    unsigned long status;
  };
  Atom motif_atom = XInternAtom(m_display, "_MOTIF_WM_HINTS", False);
  MotifHints hints{
      .flags = (1UL << 1U), // MWM_HINTS_DECORATIONS
      .functions = 0,
      .decorations = 0,     // 0 = borderless custom titlebar
      .inputMode = 0,
      .status = 0
  };
  XChangeProperty(m_display, m_window, motif_atom, motif_atom, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(&hints), 5);

  Atom net_wm_type = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE", False);
  Atom net_wm_type_dialog = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
  XChangeProperty(m_display, m_window, net_wm_type, XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(&net_wm_type_dialog), 1);

  Atom net_wm_state = XInternAtom(m_display, "_NET_WM_STATE", False);
  Atom net_wm_state_modal = XInternAtom(m_display, "_NET_WM_STATE_MODAL", False);
  XChangeProperty(m_display, m_window, net_wm_state, XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(&net_wm_state_modal), 1);

  Atom wm_delete = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
  Atom protocols[] = {wm_delete};
  XSetWMProtocols(m_display, m_window, protocols, 1);

  XStoreName(m_display, m_window, "Settings");

  m_back_buffer = XCreatePixmap(
      m_display, m_window, static_cast<unsigned int>(m_width),
      static_cast<unsigned int>(m_height),
      static_cast<unsigned int>(DefaultDepth(m_display, m_screen)));

  m_gc = XCreateGC(m_display, m_window, 0, nullptr);

  m_open = true;
  m_close_hovered = false;
  m_dragging_titlebar = false;

  m_engine.open();

  XMapRaised(m_display, m_window);
  XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
  XFlush(m_display);
  render();
}

void X11SettingsDialog::close() {
  m_engine.close();
  const bool had_active = m_open || m_window != 0;
  if (m_display != nullptr) {
    if (m_window != 0) {
      const Window old_w = m_window;
      XUngrabPointer(m_display, CurrentTime);
      XUngrabKeyboard(m_display, CurrentTime);
      XDestroyWindow(m_display, m_window);
      m_window = 0;
      XSync(m_display, False);
      XEvent ev{};
      while (XCheckWindowEvent(m_display, old_w, 0xFFFFFFFF, &ev));
    }
    if (m_back_buffer != 0) {
      XFreePixmap(m_display, m_back_buffer);
      m_back_buffer = 0;
    }
    if (m_gc != nullptr) {
      XFreeGC(m_display, m_gc);
      m_gc = nullptr;
    }
    m_open = false;
    m_dragging_titlebar = false;
    m_close_hovered = false;
    if (had_active && m_parent_window != 0) {
      XSetInputFocus(m_display, m_parent_window, RevertToParent, CurrentTime);
      XRaiseWindow(m_display, m_parent_window);
      XFlush(m_display);
    }
    m_parent_window = 0;
    if (had_active && m_on_close_callback) {
      m_on_close_callback();
    }
  } else {
    m_open = false;
    m_window = 0;
    m_parent_window = 0;
  }
}

void X11SettingsDialog::shutdown() {
  close();
  for (auto &pair : m_svg_cache) {
    if (pair.second) {
      XDestroyImage(pair.second);
    }
  }
  m_svg_cache.clear();

  m_title_font.reset();
  m_large_font.reset();
  m_ui_font.reset();
  m_small_font.reset();
  m_display = nullptr;
  m_parent_window = 0;
}

void X11SettingsDialog::apply_theme(const UI::Theme::StudioTheme &theme) {
  m_theme = theme;
  m_engine.set_theme(theme);
  if (m_open) {
    render();
  }
}

unsigned long
X11SettingsDialog::color_to_pixel(const UI::Theme::Color &color) const {
  if (m_display == nullptr) {
    return 0;
  }
  XColor x_color{};
  x_color.red = static_cast<unsigned short>(color.red * 257U);
  x_color.green = static_cast<unsigned short>(color.green * 257U);
  x_color.blue = static_cast<unsigned short>(color.blue * 257U);
  x_color.flags = DoRed | DoGreen | DoBlue;
  if (XAllocColor(m_display, DefaultColormap(m_display, m_screen), &x_color) ==
      0) {
    return BlackPixel(m_display, m_screen);
  }
  return x_color.pixel;
}

void X11SettingsDialog::fill_rect(Drawable drawable, const UI::Rect &rect,
                                  unsigned long pixel) {
  if (rect.is_empty() || m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XSetForeground(m_display, m_gc, pixel);
  XFillRectangle(m_display, drawable, m_gc, round_to_int(rect.x),
                 round_to_int(rect.y),
                 static_cast<unsigned int>(std::max(round_to_int(rect.width), 0)),
                 static_cast<unsigned int>(std::max(round_to_int(rect.height), 0)));
}

void X11SettingsDialog::draw_rect(Drawable drawable, const UI::Rect &rect,
                                  unsigned long pixel) {
  if (rect.is_empty() || m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XSetForeground(m_display, m_gc, pixel);
  XDrawRectangle(
      m_display, drawable, m_gc, round_to_int(rect.x), round_to_int(rect.y),
      static_cast<unsigned int>(std::max(round_to_int(rect.width) - 1, 0)),
      static_cast<unsigned int>(std::max(round_to_int(rect.height) - 1, 0)));
}

void X11SettingsDialog::fill_rounded_rect(Drawable drawable,
                                          const UI::Rect &rect,
                                          unsigned long fill_px, float radius,
                                          unsigned long bg_px) {
  if (rect.is_empty() || m_display == nullptr || m_gc == nullptr) {
    return;
  }
  unsigned long opaque_color = (255UL << 24) | (fill_px & 0xFFFFFF);
  unsigned long opaque_bg = (255UL << 24) | (bg_px & 0xFFFFFF);

  Utility::X11Rounded::X11Rounded::fillRoundedRectAA(
      m_display, drawable, m_gc, round_to_int(rect.x), round_to_int(rect.y),
      std::max(round_to_int(rect.width), 0),
      std::max(round_to_int(rect.height), 0), round_to_int(radius),
      opaque_color, opaque_bg, true);
}

void X11SettingsDialog::draw_rounded_rect(Drawable drawable,
                                          const UI::Rect &rect,
                                          unsigned long border_px,
                                          float radius) {
  if (rect.is_empty() || m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XSetForeground(m_display, m_gc, border_px);
  Utility::X11Rounded::X11Rounded::drawRoundedRect(
      m_display, drawable, m_gc, round_to_int(rect.x), round_to_int(rect.y),
      std::max(round_to_int(rect.width), 0),
      std::max(round_to_int(rect.height), 0), round_to_int(radius));
}

void X11SettingsDialog::draw_line_segment(Drawable drawable, int x1, int y1,
                                          int x2, int y2,
                                          unsigned long pixel) {
  if (m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XSetForeground(m_display, m_gc, pixel);
  XDrawLine(m_display, drawable, m_gc, x1, y1, x2, y2);
}

void X11SettingsDialog::set_clip(const UI::Rect &rect) {
  if (m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XRectangle xrect;
  xrect.x = static_cast<short>(round_to_int(rect.x));
  xrect.y = static_cast<short>(round_to_int(rect.y));
  xrect.width =
      static_cast<unsigned short>(std::max(round_to_int(rect.width), 0));
  xrect.height =
      static_cast<unsigned short>(std::max(round_to_int(rect.height), 0));
  XSetClipRectangles(m_display, m_gc, 0, 0, &xrect, 1, Unsorted);
}

void X11SettingsDialog::reset_clip() {
  if (m_display == nullptr || m_gc == nullptr) {
    return;
  }
  XSetClipMask(m_display, m_gc, None);
}

float X11SettingsDialog::get_text_width(AntialiasedFont &font,
                                        std::string_view text) const {
  return static_cast<float>(font.getTextWidth(std::string{text}));
}

void X11SettingsDialog::draw_text(Drawable drawable, AntialiasedFont &font,
                                  std::string_view text, float x, float y,
                                  const std::string &hex_color) {
  if (text.empty() || !font.isValid()) {
    return;
  }
  const int baseline = round_to_int(
      y - static_cast<float>(font.getAscent() + font.getDescent()) * 0.5F +
      static_cast<float>(font.getAscent()));
  font.drawString(drawable, hex_color, round_to_int(x), baseline,
                  std::string{text});
}

void X11SettingsDialog::draw_icon(Drawable drawable, const std::string &path,
                                  int center_x, int center_y, int size,
                                  const UI::Theme::Color &color,
                                  const UI::Theme::Color &background) {
  if (size <= 0 || m_display == nullptr || path.empty() || m_gc == nullptr) {
    return;
  }

  std::filesystem::path resolved_path{path};
  if (!std::filesystem::exists(resolved_path) && !m_icon_asset_root.empty()) {
    std::string rel_str = path;
    if (rel_str.starts_with("Assets/icons/")) {
      rel_str = rel_str.substr(13);
    } else if (rel_str.starts_with("Assets/")) {
      rel_str = rel_str.substr(7);
    }
    const auto candidate = m_icon_asset_root / rel_str;
    if (std::filesystem::exists(candidate)) {
      resolved_path = candidate;
    } else {
      const auto themed = m_icon_asset_root / path;
      if (std::filesystem::exists(themed)) {
        resolved_path = themed;
      }
    }
  }

  if (!std::filesystem::exists(resolved_path)) {
    const auto cwd_candidate = std::filesystem::current_path() / path;
    if (std::filesystem::exists(cwd_candidate)) {
      resolved_path = cwd_candidate;
    }
  }

  if (!std::filesystem::exists(resolved_path)) {
    return;
  }

  const int draw_x = center_x - size / 2;
  const int draw_y = center_y - size / 2;

  const std::string cache_key =
      resolved_path.string() + "@" + std::to_string(size) + "#" +
      to_hex_color(color) + "/" + to_hex_color(background);

  XImage *image = nullptr;
  auto it = m_svg_cache.find(cache_key);
  if (it != m_svg_cache.end()) {
    image = it->second;
  } else {
    auto document = lunasvg::Document::loadFromFile(resolved_path.string());
    if (!document) {
      return;
    }
    auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size),
                                           static_cast<std::uint32_t>(size));
    if (bitmap.isNull()) {
      return;
    }

    char *x11_data = static_cast<char *>(std::malloc(size * size * 4));
    if (!x11_data) {
      return;
    }

    const uint32_t bg_r = static_cast<uint32_t>(background.red);
    const uint32_t bg_g = static_cast<uint32_t>(background.green);
    const uint32_t bg_b = static_cast<uint32_t>(background.blue);

    const uint32_t tint_r = static_cast<uint32_t>(color.red);
    const uint32_t tint_g = static_cast<uint32_t>(color.green);
    const uint32_t tint_b = static_cast<uint32_t>(color.blue);

    const uint32_t *src = reinterpret_cast<const uint32_t *>(bitmap.data());
    uint32_t *dst = reinterpret_cast<uint32_t *>(x11_data);

    for (int i = 0; i < size * size; ++i) {
      uint32_t pixel = src[i];
      uint32_t a = (pixel >> 24) & 0xFF;

      const uint32_t out_r = (tint_r * a + bg_r * (255 - a)) / 255;
      const uint32_t out_g = (tint_g * a + bg_g * (255 - a)) / 255;
      const uint32_t out_b = (tint_b * a + bg_b * (255 - a)) / 255;

      dst[i] = (out_r << 16) | (out_g << 8) | out_b;
    }

    image = XCreateImage(
        m_display, DefaultVisual(m_display, m_screen),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)), ZPixmap,
        0, x11_data, size, size, 32, size * 4);
    if (!image) {
      std::free(x11_data);
      return;
    }
    m_svg_cache[cache_key] = image;
  }

  if (image) {
    XPutImage(m_display, drawable, m_gc, image, 0, 0, draw_x, draw_y, size,
              size);
  }
}

void X11SettingsDialog::render() {
  if (!m_open || m_display == nullptr || m_back_buffer == 0 ||
      m_gc == nullptr) {
    return;
  }

  const float scale = m_dpi_scale;
  const auto layout = m_engine.calculate_layout(
      static_cast<float>(m_width), static_cast<float>(m_height), scale);

  const auto &theme = m_theme;
  const UI::Theme::Color dialog_bg = theme.panel_background;
  const UI::Theme::Color border_col = theme.titlebar_border;
  const unsigned long dialog_px = color_to_pixel(dialog_bg);
  const unsigned long border_px = color_to_pixel(border_col);
  const unsigned long titlebar_px = color_to_pixel(theme.titlebar_background);

  auto centered_x = [&](AntialiasedFont &font, const UI::Rect &rc,
                        std::string_view text) -> float {
    const float tw = get_text_width(font, text);
    return rc.x + (rc.width - tw) * 0.5F;
  };

  // 1. Fill dialog background
  fill_rect(m_back_buffer, UI::Rect{0.0F, 0.0F, static_cast<float>(m_width),
                                    static_cast<float>(m_height)},
            dialog_px);

  // 2. Header (Custom Titlebar)
  fill_rect(m_back_buffer, layout.header_bounds, titlebar_px);
  draw_line_segment(m_back_buffer, 0,
                    round_to_int(layout.header_bounds.bottom() - 1.0F),
                    m_width, round_to_int(layout.header_bounds.bottom() - 1.0F),
                    border_px);

  // Titlebar Gear Icon
  draw_icon(m_back_buffer, "Assets/icons/gear.svg",
            round_to_int(layout.header_bounds.x + 14.0F * scale + 7.5F * scale),
            round_to_int(layout.header_bounds.y +
                         layout.header_bounds.height * 0.5F),
            std::max(round_to_int(16.0F * scale), 12), theme.text_secondary,
            theme.titlebar_background);

  // Titlebar Title Text
  if (m_title_font && m_title_font->isValid()) {
    draw_text(m_back_buffer, *m_title_font, "Settings", layout.title_bounds.x,
              layout.title_bounds.y + layout.title_bounds.height * 0.5F,
              to_hex_color(theme.text_primary));
  }

  // Titlebar Close Button (Red on hover)
  if (m_close_hovered) {
    fill_rect(m_back_buffer, layout.close_btn_bounds,
              color_to_pixel(UI::Theme::Color{232, 17, 35, 255}));
  }
  {
    const float cx =
        layout.close_btn_bounds.x + layout.close_btn_bounds.width * 0.5F;
    const float cy =
        layout.close_btn_bounds.y + layout.close_btn_bounds.height * 0.5F;
    const float h = 5.0F * scale;
    const unsigned long col = color_to_pixel(
        m_close_hovered ? UI::Theme::Color{255, 255, 255, 255}
                        : theme.text_secondary);
    draw_line_segment(m_back_buffer, round_to_int(cx - h), round_to_int(cy - h),
                      round_to_int(cx + h), round_to_int(cy + h), col);
    draw_line_segment(m_back_buffer, round_to_int(cx - h), round_to_int(cy + h),
                      round_to_int(cx + h), round_to_int(cy - h), col);
  }

  // 3. Search Bar
  if (!layout.search_bar_bounds.is_empty()) {
    const bool search_focused = m_engine.get_search_input().get_state().focused;
    fill_rounded_rect(m_back_buffer, layout.search_bar_bounds,
                      color_to_pixel(theme.command_center_background),
                      3.0F * scale, dialog_px);
    draw_rounded_rect(
        m_back_buffer, layout.search_bar_bounds,
        color_to_pixel(search_focused ? theme.accent
                                      : theme.command_center_border),
        3.0F * scale);
    draw_icon(
        m_back_buffer, "Assets/icons/search.svg",
        round_to_int(layout.search_bar_bounds.x + 10.0F * scale + 7.0F * scale),
        round_to_int(layout.search_bar_bounds.y +
                     layout.search_bar_bounds.height * 0.5F),
        std::max(round_to_int(14.0F * scale), 11), theme.text_secondary,
        theme.command_center_background);

    if (m_ui_font) {
      const std::string query = m_engine.get_search_query();
      draw_text(m_back_buffer, *m_ui_font,
                query.empty() ? "Search settings" : query,
                layout.search_bar_bounds.x + 32.0F * scale,
                layout.search_bar_bounds.y +
                    layout.search_bar_bounds.height * 0.5F,
                to_hex_color(query.empty() ? theme.text_secondary
                                           : theme.text_primary));

      if (m_engine.get_search_input().is_caret_visible()) {
        const std::string prefix{m_engine.get_search_input().get_text_before_cursor()};
        const float tw = get_text_width(*m_ui_font, prefix);
        const int cx = round_to_int(layout.search_bar_bounds.x + 32.0F * scale + tw + 1.0F);
        const int top = round_to_int(layout.search_bar_bounds.y +
                                     (layout.search_bar_bounds.height - 16.0F * scale) * 0.5F);
        const int bot = top + round_to_int(16.0F * scale);
        draw_line_segment(m_back_buffer, cx, top, cx, bot, color_to_pixel(theme.text_primary));
      }
    }
    if (!layout.search_clear_btn_bounds.is_empty() && m_small_font) {
      draw_text(m_back_buffer, *m_small_font, "x",
                layout.search_clear_btn_bounds.x + 4.0F * scale,
                layout.search_clear_btn_bounds.y +
                    layout.search_clear_btn_bounds.height * 0.5F,
                to_hex_color(theme.text_secondary));
    }
  }

  // 4. Scope Tabs: User | Workspace
  if (m_ui_font) {
    const bool user_active =
        m_engine.get_active_scope() == Zenvra::Settings::SettingsScope::User;
    draw_text(m_back_buffer, *m_ui_font, "User", layout.user_tab_bounds.x,
              layout.user_tab_bounds.y + layout.user_tab_bounds.height * 0.5F,
              to_hex_color(user_active ? theme.text_primary
                                       : theme.text_secondary));
    draw_text(m_back_buffer, *m_ui_font, "Workspace",
              layout.workspace_tab_bounds.x,
              layout.workspace_tab_bounds.y +
                  layout.workspace_tab_bounds.height * 0.5F,
              to_hex_color(!user_active ? theme.text_primary
                                        : theme.text_secondary));
  }

  // Active Tab Underline & Separators
  if (!layout.tab_underline_bounds.is_empty()) {
    fill_rect(m_back_buffer, layout.tab_underline_bounds,
              color_to_pixel(theme.accent));
  }
  if (!layout.header_separator_bounds.is_empty()) {
    draw_line_segment(m_back_buffer, 0,
                      round_to_int(layout.header_separator_bounds.y), m_width,
                      round_to_int(layout.header_separator_bounds.y), border_px);
  }
  if (!layout.sidebar_divider_bounds.is_empty()) {
    draw_line_segment(m_back_buffer,
                      round_to_int(layout.sidebar_divider_bounds.x),
                      round_to_int(layout.sidebar_divider_bounds.y),
                      round_to_int(layout.sidebar_divider_bounds.x), m_height,
                      border_px);
  }

  // 5. Sidebar Category Tree
  set_clip(layout.sidebar_bounds);
  for (const auto &item : layout.category_items) {
    if (item.bounds.bottom() < layout.sidebar_bounds.y ||
        item.bounds.y > layout.sidebar_bounds.bottom()) {
      continue;
    }
    const bool is_active =
        (item.id == m_engine.get_active_category() ||
         item.name == m_engine.get_active_category()) &&
        m_engine.get_search_query().empty();
    const bool is_hovered = (item.id == layout.hovered_category ||
                             item.name == layout.hovered_category);

    if (is_active) {
      draw_rounded_rect(m_back_buffer, item.bounds, color_to_pixel(theme.accent),
                        2.0F * scale);
    } else if (is_hovered) {
      fill_rounded_rect(m_back_buffer, item.bounds, color_to_pixel(theme.hover),
                        2.0F * scale, dialog_px);
    }
    if (item.has_children) {
      const int ch_sz = std::max(round_to_int(10.0F * scale), 8);
      draw_icon(m_back_buffer,
                item.is_expanded ? "Assets/icons/chevron-down.svg"
                                 : "Assets/icons/chevron-right.svg",
                round_to_int(item.bounds.x + 6.0F * scale + ch_sz * 0.5F),
                round_to_int(item.bounds.y + item.bounds.height * 0.5F), ch_sz,
                theme.text_secondary, dialog_bg);
    }
    if (m_small_font) {
      const float indent = item.has_children
                               ? (item.bounds.x + 22.0F * scale)
                               : (item.depth == 0
                                      ? (item.bounds.x + 22.0F * scale)
                                      : (item.bounds.x + 36.0F * scale));
      draw_text(m_back_buffer, *m_small_font, item.name, indent,
                item.bounds.y + item.bounds.height * 0.5F,
                to_hex_color(is_active ? theme.text_primary
                                       : theme.text_secondary));
    }
  }
  reset_clip();

  // Sidebar Scrollbar
  if (!layout.sidebar_scrollbar_track.is_empty()) {
    fill_rounded_rect(m_back_buffer, layout.sidebar_scrollbar_track,
                      color_to_pixel(UI::Theme::Color{18, 19, 22, 160}),
                      2.0F * scale, dialog_px);
  }
  if (!layout.sidebar_scrollbar_thumb.is_empty()) {
    const bool hot = layout.sidebar_scrollbar_thumb_hovered ||
                     m_engine.is_dragging_sidebar_scrollbar();
    fill_rounded_rect(
        m_back_buffer, layout.sidebar_scrollbar_thumb,
        color_to_pixel(hot ? UI::Theme::Color{95, 100, 110, 255}
                           : (theme.is_dark ? UI::Theme::Color{65, 70, 80, 255}
                                            : theme.hover)),
        2.0F * scale, dialog_px);
  }

  // 6. Content Area: Section Headers + Setting Rows
  auto &service = Zenvra::Settings::SettingsService::instance();
  set_clip(layout.content_bounds);

  for (const auto &hdr : layout.section_headers) {
    if (hdr.bounds.bottom() < layout.content_bounds.y ||
        hdr.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }
    if (m_large_font) {
      draw_text(m_back_buffer, *m_large_font, hdr.title, hdr.bounds.x,
                hdr.bounds.y + hdr.bounds.height * 0.5F,
                to_hex_color(theme.text_primary));
    }
  }

  for (const auto &row : layout.rows) {
    if (!row.def)
      continue;
    if (row.bounds.bottom() < layout.content_bounds.y ||
        row.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }
    const auto *def = row.def;

    // Focused row indicator + gear
    if (row.is_focused) {
      draw_rounded_rect(m_back_buffer, row.focus_box_bounds,
                        color_to_pixel(theme.accent), 2.0F * scale);
      draw_icon(m_back_buffer, "Assets/icons/gear.svg",
                round_to_int(row.gear_btn_bounds.x +
                             row.gear_btn_bounds.width * 0.5F),
                round_to_int(row.gear_btn_bounds.y +
                             row.gear_btn_bounds.height * 0.5F),
                std::max(round_to_int(15.0F * scale), 12),
                theme.text_secondary, dialog_bg);
    }

    // Setting Title: "Category: Title" (+ "(Modified)")
    if (m_ui_font) {
      const std::string title = def->category + ": " + def->title +
                                (row.is_modified ? "  (Modified)" : "");
      draw_text(m_back_buffer, *m_ui_font, title, row.label_bounds.x,
                row.label_bounds.y + row.label_bounds.height * 0.5F,
                to_hex_color(theme.text_primary));
    }
    if (m_small_font && !row.description_bounds.is_empty()) {
      draw_text(m_back_buffer, *m_small_font, def->description,
                row.description_bounds.x,
                row.description_bounds.y + row.description_bounds.height * 0.5F,
                to_hex_color(theme.text_secondary));
    }

    // Type-specific controls
    if (def->type == Zenvra::Settings::SettingType::Boolean) {
      const bool val = service.get<bool>(def->id);
      const UI::Theme::Color bg =
          val ? theme.accent
              : (row.is_checkbox_hovered ? theme.command_center_background
                                         : UI::Theme::Color{24, 25, 28, 255});
      const UI::Theme::Color bd = (val || row.is_checkbox_hovered)
                                      ? theme.accent
                                      : theme.command_center_border;
      fill_rounded_rect(m_back_buffer, row.checkbox_bounds,
                        color_to_pixel(bg), 2.0F * scale, dialog_px);
      draw_rounded_rect(m_back_buffer, row.checkbox_bounds,
                        color_to_pixel(bd), 2.0F * scale);
      if (val) {
        const unsigned long white =
            color_to_pixel(UI::Theme::Color{255, 255, 255, 255});
        const int cx = round_to_int(row.checkbox_bounds.x);
        const int cy = round_to_int(row.checkbox_bounds.y);
        const int s = std::max(round_to_int(scale), 1);
        draw_line_segment(m_back_buffer, cx + 4 * s, cy + 9 * s, cx + 7 * s,
                          cy + 13 * s, white);
        draw_line_segment(m_back_buffer, cx + 7 * s, cy + 13 * s, cx + 14 * s,
                          cy + 5 * s, white);
      }
    } else if (def->id == "editor.fontFamily") {
      const bool is_open =
          m_engine.get_dropdown().is_open() &&
          (m_engine.get_dropdown().get_owner_id() == "editor.fontFamily");
      fill_rounded_rect(m_back_buffer, row.input_bounds,
                        color_to_pixel(theme.command_center_background),
                        2.5F * scale, dialog_px);
      draw_rounded_rect(
          m_back_buffer, row.input_bounds,
          color_to_pixel(is_open ? theme.accent
                                 : (row.is_input_hovered
                                        ? theme.hover
                                        : theme.command_center_border)),
          2.5F * scale);
      if (m_ui_font) {
        const std::string cur = service.get<std::string>(def->id);
        draw_text(m_back_buffer, *m_ui_font, cur,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  to_hex_color(theme.text_primary));
      }
      draw_icon(
          m_back_buffer, "Assets/icons/chevron-down.svg",
          round_to_int(row.dropdown_btn_bounds.x +
                       row.dropdown_btn_bounds.width * 0.5F),
          round_to_int(row.dropdown_btn_bounds.y +
                       row.dropdown_btn_bounds.height * 0.5F),
          std::max(round_to_int(9.0F * scale), 8), theme.text_secondary,
          theme.command_center_background);
    } else if (def->id == "workbench.mascot.image") {
      const std::string img_path = service.get<std::string>(def->id);
      const UI::Theme::Color thumb_bg{20, 21, 24, 255};
      fill_rounded_rect(m_back_buffer, row.thumbnail_bounds,
                        color_to_pixel(thumb_bg), 3.0F * scale, dialog_px);
      draw_rounded_rect(
          m_back_buffer, row.thumbnail_bounds,
          color_to_pixel((row.is_thumbnail_hovered || row.is_focused)
                             ? theme.accent
                             : theme.command_center_border),
          3.0F * scale);

      draw_icon(
          m_back_buffer, "Assets/icons/image.svg",
          round_to_int(row.thumbnail_bounds.x +
                       row.thumbnail_bounds.width * 0.5F),
          round_to_int(row.thumbnail_bounds.y +
                       row.thumbnail_bounds.height * 0.5F - 8.0F * scale),
          std::max(round_to_int(22.0F * scale), 16), theme.text_secondary,
          thumb_bg);

      if (m_small_font) {
        std::string mode;
        try {
          mode = service.get<std::string>("workbench.mascot.renderMode");
        } catch (...) {
          mode = "default";
        }
        std::string ext = (mode == "ascii") ? "ASCII" : "";
        if (ext.empty()) {
          const auto dot = img_path.find_last_of('.');
          ext = (dot == std::string::npos) ? "PNG" : img_path.substr(dot + 1);
          for (auto &c : ext)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          if (ext.empty())
            ext = "PNG";
        }
        const UI::Rect badge{
            row.thumbnail_bounds.x + 3.0F * scale,
            row.thumbnail_bounds.bottom() - 13.0F * scale - 3.0F * scale,
            (ext == "ASCII" ? 38.0F : 28.0F) * scale, 13.0F * scale};
        fill_rounded_rect(m_back_buffer, badge,
                          color_to_pixel(UI::Theme::Color{12, 14, 18, 255}),
                          2.0F * scale, color_to_pixel(thumb_bg));
        draw_text(m_back_buffer, *m_small_font, ext, badge.x + 4.0F * scale,
                  badge.y + badge.height * 0.5F, to_hex_color(theme.text_secondary));
        std::string disp = img_path.empty()
                               ? "zenvra_logo.png (Default Mascot)"
                               : img_path;
        draw_text(m_back_buffer, *m_small_font, disp,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  to_hex_color(theme.text_primary));
      }

      auto button = [&](const UI::Rect &rc, std::string_view label,
                        bool hovered, bool enabled = true) {
        fill_rounded_rect(
            m_back_buffer, rc,
            color_to_pixel(hovered && enabled ? theme.hover
                                              : theme.command_center_background),
            2.5F * scale, dialog_px);
        draw_rounded_rect(
            m_back_buffer, rc,
            color_to_pixel(hovered && enabled ? theme.accent
                                              : theme.command_center_border),
            2.5F * scale);
        if (m_ui_font) {
          draw_text(m_back_buffer, *m_ui_font, label,
                    centered_x(*m_ui_font, rc, label), rc.y + rc.height * 0.5F,
                    to_hex_color(enabled ? theme.text_primary
                                         : theme.text_secondary));
        }
      };
      button(row.browse_btn_bounds, "Browse...", row.is_browse_hovered);
      button(row.reset_btn_bounds, "Reset", row.is_reset_hovered,
             row.is_modified);
      if (m_small_font) {
        draw_text(m_back_buffer, *m_small_font,
                  "Drop image here (.png, .jpg, .bmp)",
                  row.reset_btn_bounds.right() + 12.0F * scale,
                  row.browse_btn_bounds.y +
                      row.browse_btn_bounds.height * 0.5F,
                  to_hex_color(theme.text_secondary));
      }
    } else if (def->id == "workbench.mascot.renderMode") {
      const bool is_ascii =
          (service.get<std::string>("workbench.mascot.renderMode") == "ascii");
      auto switch_btn = [&](const UI::Rect &rc, std::string_view label,
                            bool active, bool hovered) {
        const UI::Theme::Color bg =
            active ? theme.accent
                   : (hovered ? theme.hover : theme.command_center_background);
        const UI::Theme::Color bd =
            active ? theme.accent
                   : (hovered ? theme.accent : theme.command_center_border);
        fill_rounded_rect(m_back_buffer, rc, color_to_pixel(bg), 3.0F * scale,
                          dialog_px);
        draw_rounded_rect(m_back_buffer, rc, color_to_pixel(bd), 3.0F * scale);
        if (m_ui_font) {
          draw_text(m_back_buffer, *m_ui_font, label,
                    centered_x(*m_ui_font, rc, label), rc.y + rc.height * 0.5F,
                    active ? "#ffffff" : to_hex_color(theme.text_secondary));
        }
      };
      switch_btn(row.switch_default_btn_bounds, "Default (Solid)", !is_ascii,
                 row.is_switch_default_hovered);
      switch_btn(row.switch_ascii_btn_bounds, "ASCII Art", is_ascii,
                 row.is_switch_ascii_hovered);
    } else if ((def->type == Zenvra::Settings::SettingType::Integer ||
                def->type == Zenvra::Settings::SettingType::Float ||
                def->type == Zenvra::Settings::SettingType::String) &&
               def->id != "workbench.mascot.image") {
      const bool is_editing =
          (m_engine.get_editing_setting_id() == def->id);
      fill_rounded_rect(m_back_buffer, row.input_bounds,
                        color_to_pixel(theme.command_center_background),
                        2.5F * scale, dialog_px);
      draw_rounded_rect(
          m_back_buffer, row.input_bounds,
          color_to_pixel(is_editing ? theme.accent
                                    : (row.is_input_hovered
                                           ? theme.hover
                                           : theme.command_center_border)),
          2.5F * scale);
      if (m_ui_font) {
        const std::string val =
            is_editing ? m_engine.get_editing_text()
                       : service.get(def->id).to_display_string();
        draw_text(m_back_buffer, *m_ui_font, val,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  to_hex_color(theme.text_primary));
        if (is_editing && m_engine.is_caret_visible()) {
          const float tw = get_text_width(*m_ui_font, val);
          const int cx =
              round_to_int(row.input_bounds.x + 8.0F * scale + tw + 1.0F);
          const int top = round_to_int(
              row.input_bounds.y +
              (row.input_bounds.height - 16.0F * scale) * 0.5F);
          const int bot = top + round_to_int(16.0F * scale);
          draw_line_segment(m_back_buffer, cx, top, cx, bot,
                            color_to_pixel(theme.text_primary));
        }
      }
    } else if (def->type == Zenvra::Settings::SettingType::Enum &&
               def->id != "workbench.mascot.renderMode") {
      const bool is_open =
          m_engine.get_dropdown().is_open() &&
          (m_engine.get_dropdown().get_owner_id() == def->id);
      fill_rounded_rect(
          m_back_buffer, row.option_btn_bounds,
          color_to_pixel((row.is_option_hovered || is_open)
                             ? theme.hover
                             : theme.command_center_background),
          2.5F * scale, dialog_px);
      draw_rounded_rect(
          m_back_buffer, row.option_btn_bounds,
          color_to_pixel((row.is_option_hovered || is_open)
                             ? theme.accent
                             : theme.command_center_border),
          2.5F * scale);
      if (m_ui_font) {
        const std::string cur = service.get<std::string>(def->id);
        draw_text(m_back_buffer, *m_ui_font, cur,
                  row.option_btn_bounds.x + 10.0F * scale,
                  row.option_btn_bounds.y +
                      row.option_btn_bounds.height * 0.5F,
                  to_hex_color(theme.text_primary));
      }
      draw_icon(
          m_back_buffer, "Assets/icons/chevron-down.svg",
          round_to_int(row.option_btn_bounds.right() - 16.0F * scale),
          round_to_int(row.option_btn_bounds.y +
                       row.option_btn_bounds.height * 0.5F),
          std::max(round_to_int(9.0F * scale), 8), theme.text_secondary,
          theme.command_center_background);
    }
  }
  reset_clip();

  // 7. Content Scrollbar
  if (!layout.scrollbar_track.is_empty()) {
    fill_rounded_rect(m_back_buffer, layout.scrollbar_track,
                      color_to_pixel(UI::Theme::Color{18, 19, 22, 160}),
                      3.0F * scale, dialog_px);
  }
  if (!layout.scrollbar_thumb.is_empty()) {
    const bool hot = layout.scrollbar_thumb_hovered ||
                     m_engine.is_dragging_scrollbar();
    fill_rounded_rect(
        m_back_buffer, layout.scrollbar_thumb,
        color_to_pixel(hot ? UI::Theme::Color{100, 106, 120, 255}
                           : (theme.is_dark ? UI::Theme::Color{65, 70, 80, 255}
                                            : theme.hover)),
        3.0F * scale, dialog_px);
  }

  // 8. Dropdown Popover (On top of content)
  {
    const auto &dd = m_engine.get_dropdown();
    if (dd.is_open() && m_small_font && m_ui_font) {
      const UI::Rect &b = dd.get_bounds();
      if (!b.is_empty()) {
        const unsigned long dd_bg = color_to_pixel(theme.panel_background);
        const unsigned long dd_border = color_to_pixel(theme.titlebar_border);
        fill_rounded_rect(m_back_buffer, b, dd_bg, 6.0F * scale, dialog_px);
        draw_rounded_rect(m_back_buffer, b, dd_border, 6.0F * scale);
        set_clip(b);
        for (const auto &it : dd.get_layout_items()) {
          if (it.bounds.is_empty())
            continue;
          if (it.is_hovered) {
            fill_rounded_rect(m_back_buffer, it.bounds,
                              color_to_pixel(theme.hover), 3.0F * scale, dd_bg);
          }
          float text_x = it.bounds.x + 10.0F * scale;
          if (!it.item.preview_colors.empty()) {
            float swatch_x = text_x;
            const float swatch_w = 14.0F * scale;
            const float swatch_h = 14.0F * scale;
            const float swatch_y =
                it.bounds.y + (it.bounds.height - swatch_h) * 0.5F;
            for (const auto &color : it.item.preview_colors) {
              fill_rounded_rect(m_back_buffer,
                                UI::Rect{swatch_x, swatch_y, swatch_w, swatch_h},
                                color_to_pixel(color), 2.0F * scale, dd_bg);
              swatch_x += swatch_w + 3.0F * scale;
            }
            text_x = swatch_x + 6.0F * scale;
          }
          draw_text(m_back_buffer, *m_small_font, it.item.label, text_x,
                    it.bounds.y + it.bounds.height * 0.5F,
                    to_hex_color(it.is_selected ? theme.text_primary
                                                : theme.text_secondary));
          if (!it.item.description.empty()) {
            const float desc_w =
                get_text_width(*m_small_font, it.item.description);
            draw_text(m_back_buffer, *m_small_font, it.item.description,
                      it.bounds.right() - desc_w - 10.0F * scale,
                      it.bounds.y + it.bounds.height * 0.5F,
                      to_hex_color(theme.text_secondary));
          }
        }
        reset_clip();

        const auto &dd_track = dd.get_scrollbar_track();
        const auto &dd_thumb = dd.get_scrollbar_thumb();
        if (!dd_track.is_empty() && !dd_thumb.is_empty()) {
          fill_rounded_rect(m_back_buffer, dd_track,
                            color_to_pixel(UI::Theme::Color{15, 16, 18, 160}),
                            2.0F * scale, dd_bg);
          const bool dd_hot = dd.is_scrollbar_thumb_hovered() ||
                              dd.is_dragging_scrollbar();
          fill_rounded_rect(
              m_back_buffer, dd_thumb,
              color_to_pixel(dd_hot ? UI::Theme::Color{100, 106, 120, 255}
                                    : (theme.is_dark
                                           ? UI::Theme::Color{65, 70, 80, 255}
                                           : theme.hover)),
              2.0F * scale, dd_bg);
        }
      }
    }
  }

  // 9. Outer 1px Window Border
  draw_rect(m_back_buffer, UI::Rect{0.0F, 0.0F, static_cast<float>(m_width),
                                    static_cast<float>(m_height)},
            border_px);

  // Blit backbuffer to window
  XCopyArea(m_display, m_back_buffer, m_window, m_gc, 0, 0,
            static_cast<unsigned int>(m_width),
            static_cast<unsigned int>(m_height), 0, 0);
  XFlush(m_display);
}

bool X11SettingsDialog::tick_animations() noexcept {
  if (!m_open) {
    return false;
  }
  return m_engine.tick();
}

bool X11SettingsDialog::handle_event(const XEvent &event) {
  if (!m_open || event.xany.window != m_window) {
    return false;
  }

  const auto layout = m_engine.calculate_layout(
      static_cast<float>(m_width), static_cast<float>(m_height), m_dpi_scale);

  switch (event.type) {
  case ClientMessage: {
    Atom wm_protocols = XInternAtom(m_display, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    if (event.xclient.message_type == wm_protocols &&
        static_cast<Atom>(event.xclient.data.l[0]) == wm_delete) {
      close();
      return true;
    }
    return true;
  }

  case DestroyNotify:
    if (event.xdestroywindow.window == m_window) {
      close();
      return true;
    }
    break;

  case UnmapNotify:
    if (event.xunmap.window == m_window) {
      close();
      return true;
    }
    break;

  case Expose:
    render();
    return true;

  case EnterNotify:
    XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
    return true;

  case FocusIn:
    render();
    return true;

  case MotionNotify: {
    const float mx = static_cast<float>(event.xmotion.x);
    const float my = static_cast<float>(event.xmotion.y);

    if (m_dragging_titlebar) {
      const int dx = event.xmotion.x_root - m_drag_start_root_x;
      const int dy = event.xmotion.y_root - m_drag_start_root_y;
      m_win_x = m_drag_start_win_x + dx;
      m_win_y = m_drag_start_win_y + dy;
      XMoveWindow(m_display, m_window, m_win_x, m_win_y);
      return true;
    }

    const bool new_close_h = layout.close_btn_bounds.contains(mx, my);
    if (new_close_h != m_close_hovered) {
      m_close_hovered = new_close_h;
    }
    m_engine.handle_pointer_move(mx, my, layout);
    render();
    return true;
  }

  case ButtonPress: {
    XRaiseWindow(m_display, m_window);
    XSetInputFocus(m_display, m_window, RevertToParent, CurrentTime);
    XFlush(m_display);

    const float bx = static_cast<float>(event.xbutton.x);
    const float by = static_cast<float>(event.xbutton.y);

    // Mouse wheel scrolling
    if (event.xbutton.button == Button4) {
      m_engine.handle_scroll(60.0F, layout, bx, by);
      render();
      return true;
    }
    if (event.xbutton.button == Button5) {
      m_engine.handle_scroll(-60.0F, layout, bx, by);
      render();
      return true;
    }

    if (layout.close_btn_bounds.contains(bx, by)) {
      close();
      return true;
    }

    // Titlebar dragging
    if (layout.header_bounds.contains(bx, by)) {
      m_dragging_titlebar = true;
      m_drag_start_root_x = event.xbutton.x_root;
      m_drag_start_root_y = event.xbutton.y_root;
      m_drag_start_win_x = m_win_x;
      m_drag_start_win_y = m_win_y;
      return true;
    }

    m_engine.handle_pointer_press(bx, by, layout);
    render();
    return true;
  }

  case ButtonRelease: {
    const float bx = static_cast<float>(event.xbutton.x);
    const float by = static_cast<float>(event.xbutton.y);
    m_dragging_titlebar = false;
    if (layout.close_btn_bounds.contains(bx, by)) {
      close();
      return true;
    }
    m_engine.handle_pointer_release(bx, by, layout);
    render();
    return true;
  }

  case KeyPress: {
    KeySym sym = XLookupKeysym(const_cast<XKeyEvent *>(&event.xkey), 0);
    const bool is_ctrl = (event.xkey.state & ControlMask) != 0;
    if (is_ctrl && (sym == XK_comma || sym == XK_less)) {
      close();
      return true;
    }
    if (sym == XK_Escape) {
      if (m_engine.get_dropdown().is_open()) {
        m_engine.handle_escape();
        render();
        return true;
      }
      if (!m_engine.get_editing_setting_id().empty()) {
        m_engine.handle_escape();
        render();
        return true;
      }
      if (!m_engine.get_search_query().empty()) {
        m_engine.handle_escape();
        render();
        return true;
      }
      close();
      return true;
    }
    if (sym == XK_Return || sym == XK_KP_Enter) {
      m_engine.handle_enter();
      render();
      return true;
    }
    if (sym == XK_BackSpace) {
      m_engine.handle_backspace();
      render();
      return true;
    }
    if (sym == XK_Delete) {
      if (m_engine.get_search_input().get_state().focused) {
        if (m_engine.get_search_input_mut().handle_delete()) {
          render();
          return true;
        }
      }
    }
    if (sym == XK_Left) {
      if (m_engine.get_search_input().get_state().focused) {
        const bool shift = (event.xkey.state & ShiftMask) != 0;
        if (m_engine.get_search_input_mut().handle_left(shift)) {
          render();
          return true;
        }
      }
    }
    if (sym == XK_Right) {
      if (m_engine.get_search_input().get_state().focused) {
        const bool shift = (event.xkey.state & ShiftMask) != 0;
        if (m_engine.get_search_input_mut().handle_right(shift)) {
          render();
          return true;
        }
      }
    }
    if (sym == XK_Home) {
      if (m_engine.get_search_input().get_state().focused) {
        const bool shift = (event.xkey.state & ShiftMask) != 0;
        if (m_engine.get_search_input_mut().handle_home(shift)) {
          render();
          return true;
        }
      }
    }
    if (sym == XK_End) {
      if (m_engine.get_search_input().get_state().focused) {
        const bool shift = (event.xkey.state & ShiftMask) != 0;
        if (m_engine.get_search_input_mut().handle_end(shift)) {
          render();
          return true;
        }
      }
    }
    if (sym == XK_Up) {
      m_engine.handle_key(0x26, m_dpi_scale);
      render();
      return true;
    }
    if (sym == XK_Down) {
      m_engine.handle_key(0x28, m_dpi_scale);
      render();
      return true;
    }

    char buf[32]{};
    int len = XLookupString(const_cast<XKeyEvent *>(&event.xkey), buf,
                            sizeof(buf), nullptr, nullptr);
    if (len > 0 && !std::iscntrl(static_cast<unsigned char>(buf[0]))) {
      std::string_view sv(buf, len);
      size_t i = 0;
      while (i < sv.size()) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        char32_t cp = 0;
        int bytes = 1;
        if ((c & 0x80) == 0) {
          cp = c;
          bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
          cp = c & 0x1F;
          bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
          cp = c & 0x0F;
          bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
          cp = c & 0x07;
          bytes = 4;
        }
        for (int j = 1; j < bytes && (i + j) < sv.size(); ++j) {
          cp = (cp << 6) | (static_cast<unsigned char>(sv[i + j]) & 0x3F);
        }
        i += bytes;
        m_engine.handle_char(cp);
      }
      render();
      return true;
    }
    return true;
  }

  default:
    break;
  }

  return true;
}

} // namespace Zenvra::Platform::X11::Components
