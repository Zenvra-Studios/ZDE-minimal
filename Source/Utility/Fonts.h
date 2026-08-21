#ifndef FONTS_H
#define FONTS_H

#include <iostream>
#include <string>
#include <unordered_map>


#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <usp10.h> // Uniscribe for ligatures

/**
 * @class AntialiasedFont
 * @brief Helper class to handle high-quality anti-aliased font rendering in
 * Win32 using GDI ClearType.
 */
class AntialiasedFont {
public:
  /**
   * @brief Constructor. Opens a vector font by description.
   * @param font_name Font name string (e.g., "Segoe UI", "Arial").
   * @param font_size Font size in logical units.
   */
  AntialiasedFont(const std::string &font_name, int font_size = 16,
                  int font_weight = FW_NORMAL) {
    // CLEARTYPE_NATURAL_QUALITY ensures highest sub-pixel anti-aliasing on Windows
#ifndef CLEARTYPE_NATURAL_QUALITY
#define CLEARTYPE_NATURAL_QUALITY 6
#endif
    m_font = CreateFontA(-font_size, 0, 0, 0, font_weight, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_TT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, font_name.c_str());
    if (!m_font) {
      m_font = CreateFontA(-font_size, 0, 0, 0, font_weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, font_name.c_str());
    }

    if (!m_font) {
      std::cerr << "Warning: Could not open font '" << font_name << "'"
                << std::endl;
    } else {
      HDC screen_dc = GetDC(nullptr);
      if (screen_dc) {
        HFONT old_f = (HFONT)SelectObject(screen_dc, m_font);
        TEXTMETRIC tm{};
        GetTextMetrics(screen_dc, &tm);
        m_ascent = tm.tmAscent;
        m_descent = tm.tmDescent;
        m_height = tm.tmHeight;
        SIZE char_sz{};
        GetTextExtentPoint32A(screen_dc, "X", 1, &char_sz);
        m_char_width = char_sz.cx;
        SelectObject(screen_dc, old_f);
        ReleaseDC(nullptr, screen_dc);
      }
    }
  }

  ~AntialiasedFont() {
    if (m_font) {
      DeleteObject(m_font);
    }
  }

  bool isValid() const noexcept { return m_font != nullptr; }

  void setLigaturesEnabled(bool enabled) { m_ligaturesEnabled = enabled; }
  bool isLigaturesEnabled() const { return m_ligaturesEnabled; }

  /**
   * @brief Draw a string on a device context.
   * @param hdc Device Context (HDC).
   * @param color_name Color name or hex value (e.g. "black", "#3b82f6").
   * @param x X coordinate (baseline origin).
   * @param y Y coordinate (baseline origin).
   * @param text String content to draw.
   */
  void drawString(HDC hdc, const std::string &color_name, int x, int y,
                  const std::string &text) {
    if (!m_font || !hdc || text.empty())
      return;

    COLORREF color = parseColor(color_name);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    const int draw_y = y - m_ascent;

    // Fast stack buffer conversion for UTF-8 (zero heap allocations)
    wchar_t stack_buf[512];
    wchar_t* wide_ptr = stack_buf;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), stack_buf, 512);
    std::wstring heap_wtext;
    if (wlen <= 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), nullptr, 0);
      if (wlen > 0) {
        heap_wtext.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &heap_wtext[0], wlen);
        wide_ptr = &heap_wtext[0];
      }
    }

    if (wlen > 0) {
      if (m_ligaturesEnabled) {
        SCRIPT_STRING_ANALYSIS ssa = nullptr;
        HRESULT hr = ScriptStringAnalyse(hdc, wide_ptr, wlen, static_cast<int>(wlen * 3 / 2 + 16), -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
        if (SUCCEEDED(hr)) {
          ScriptStringOut(ssa, x, draw_y, 0, NULL, 0, 0, FALSE);
          ScriptStringFree(&ssa);
        } else {
          TextOutW(hdc, x, draw_y, wide_ptr, wlen);
        }
      } else {
        TextOutW(hdc, x, draw_y, wide_ptr, wlen);
      }
    }

    SelectObject(hdc, oldFont);
  }

  int getAscent(HDC /*hdc*/ = nullptr) const {
    return m_ascent;
  }

  int getDescent(HDC /*hdc*/ = nullptr) const {
    return m_descent;
  }

  int getHeight(HDC /*hdc*/ = nullptr) const {
    return m_height;
  }

  int getTextWidth(HDC hdc, const std::string &text) const {
    if (!m_font || !hdc || text.empty())
      return 0;

    int width = 0;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

    wchar_t stack_buf[512];
    wchar_t* wide_ptr = stack_buf;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), stack_buf, 512);
    std::wstring heap_wtext;
    if (wlen <= 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), nullptr, 0);
      if (wlen > 0) {
        heap_wtext.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &heap_wtext[0], wlen);
        wide_ptr = &heap_wtext[0];
      }
    }

    if (wlen > 0) {
      if (m_ligaturesEnabled) {
        SCRIPT_STRING_ANALYSIS ssa = nullptr;
        HRESULT hr = ScriptStringAnalyse(hdc, wide_ptr, wlen, static_cast<int>(wlen * 3 / 2 + 16), -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
        if (SUCCEEDED(hr)) {
          const SIZE* pSize = ScriptString_pSize(ssa);
          if (pSize) {
            width = pSize->cx;
          }
          ScriptStringFree(&ssa);
        }
      }

      if (width == 0) {
        SIZE size{};
        GetTextExtentPoint32W(hdc, wide_ptr, wlen, &size);
        width = size.cx;
      }
    }

    SelectObject(hdc, oldFont);
    return width;
  }

private:
  HFONT m_font = nullptr;
  bool m_ligaturesEnabled = false;
  int m_ascent = 14;
  int m_descent = 4;
  int m_height = 18;
  int m_char_width = 8;

  COLORREF parseColor(const std::string &color_name) {
    if (color_name.empty())
      return RGB(255, 255, 255);

    if (color_name[0] == '#') {
      if (color_name.length() >= 7) {
        int r = std::stoi(color_name.substr(1, 2), nullptr, 16);
        int g = std::stoi(color_name.substr(3, 2), nullptr, 16);
        int b = std::stoi(color_name.substr(5, 2), nullptr, 16);
        return RGB(r, g, b);
      }
    } else {
      if (color_name == "black")
        return RGB(0, 0, 0);
      if (color_name == "white")
        return RGB(255, 255, 255);
      if (color_name == "red")
        return RGB(255, 0, 0);
      if (color_name == "green")
        return RGB(0, 255, 0);
      if (color_name == "blue")
        return RGB(0, 0, 255);
    }
    return RGB(255, 255, 255); // fallback
  }
};

#elif (defined(__unix__) || defined(__linux__)) && !defined(__APPLE__)

#include <fontconfig/fontconfig.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>

/**
 * @class AntialiasedFont
 * @brief Helper class to handle high-quality anti-aliased font rendering in raw
 * Xlib using Xft and Fontconfig.
 *
 * Wraps Xft (X FreeType) and Fontconfig to render modern anti-aliased vector
 * fonts (TrueType/OpenType) with sub-pixel quality, slight hinting (hintslight),
 * LCD filtering, and ARGB visual safety across GNOME, Wayland (XWayland), and X11.
 */
class AntialiasedFont {
public:
  /**
   * @brief Constructor. Opens a vector font by description using the default visual.
   * @param display Connection to the X server.
   * @param screen_num Screen index.
   * @param font_name Font name string in Fontconfig format (e.g. "JetBrains Mono:pixelsize=14", "sans:pixelsize=12").
   */
  AntialiasedFont(Display *display, int screen_num,
                  const std::string &font_name)
      : m_display(display), m_font(nullptr) {
    m_visual = DefaultVisual(display, screen_num);
    m_colormap = DefaultColormap(display, screen_num);
    init(screen_num, font_name);
  }

  /**
   * @brief Constructor with custom visual and colormap (e.g. for 32-bit ARGB windows).
   */
  AntialiasedFont(Display *display, int screen_num,
                  const std::string &font_name,
                  Visual *visual, Colormap colormap)
      : m_display(display), m_visual(visual), m_colormap(colormap), m_font(nullptr) {
    init(screen_num, font_name);
  }

private:
  void init(int screen_num, const std::string &font_name) {
    if (!m_display) {
      return;
    }

    FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(font_name.c_str()));
    if (!pattern) {
      pattern = FcPatternCreate();
    }

    // Force essential typography settings for GNOME / Wayland / modern Linux:
    // 1. Antialiasing enabled
    FcPatternDel(pattern, FC_ANTIALIAS);
    FcPatternAddBool(pattern, FC_ANTIALIAS, FcTrue);

    // 2. Subpixel antialiasing (RGB) for rich, crisp subpixel resolution
    FcPatternDel(pattern, FC_RGBA);
    FcPatternAddInteger(pattern, FC_RGBA, FC_RGBA_RGB);

    // 3. LCD Filter (for smooth color-balanced subpixel rendering)
#ifdef FC_LCD_FILTER
    FcPatternDel(pattern, FC_LCD_FILTER);
    FcPatternAddInteger(pattern, FC_LCD_FILTER, FC_LCD_DEFAULT);
#endif

    // 4. Slight Hinting (prevents glyph warping/distortion while maintaining sharp horizontal baselines)
    FcPatternDel(pattern, FC_HINTING);
    FcPatternAddBool(pattern, FC_HINTING, FcTrue);
    FcPatternDel(pattern, FC_HINT_STYLE);
    FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_SLIGHT);

    // 5. Use FreeType's smooth autohinter for full-bodied, organic curve rendering
    FcPatternDel(pattern, FC_AUTOHINT);
    FcPatternAddBool(pattern, FC_AUTOHINT, FcTrue);

    // 6. Disable embedded bitmaps
    FcPatternDel(pattern, FC_EMBEDDED_BITMAP);
    FcPatternAddBool(pattern, FC_EMBEDDED_BITMAP, FcFalse);

    // If visual is 32-bit ARGB (e.g. popups / transparent overlays), disable subpixel RGBA
    // and use grayscale antialiasing to prevent alpha channel destruction and color fringing.
    if (m_visual) {
      XVisualInfo vinfo_template{};
      vinfo_template.visualid = XVisualIDFromVisual(m_visual);
      int nitems = 0;
      XVisualInfo *vinfo_list = XGetVisualInfo(m_display, VisualIDMask, &vinfo_template, &nitems);
      if (vinfo_list) {
        if (nitems > 0 && vinfo_list[0].depth == 32) {
          FcPatternDel(pattern, FC_RGBA);
          FcPatternAddInteger(pattern, FC_RGBA, FC_RGBA_NONE);
        }
        XFree(vinfo_list);
      }
    }

    // Perform standard Fontconfig substitutions and merge desktop Xft settings
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    XftDefaultSubstitute(m_display, screen_num, pattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    if (match) {
      m_font = XftFontOpenPattern(m_display, match);
    }
    FcPatternDestroy(pattern);

    if (!m_font) {
      std::cerr << "Warning: Could not open font '" << font_name
                << "', falling back to system sans" << std::endl;
      FcPattern *fallback = FcPatternCreate();
      FcPatternAddString(fallback, FC_FAMILY, reinterpret_cast<const FcChar8 *>("sans-serif"));
      FcPatternAddDouble(fallback, FC_PIXEL_SIZE, 12.0);
      FcPatternAddBool(fallback, FC_ANTIALIAS, FcTrue);
      FcPatternAddBool(fallback, FC_HINTING, FcTrue);
      FcPatternAddInteger(fallback, FC_HINT_STYLE, FC_HINT_SLIGHT);
#ifdef FC_LCD_FILTER
      FcPatternAddInteger(fallback, FC_LCD_FILTER, FC_LCD_DEFAULT);
#endif
      FcConfigSubstitute(nullptr, fallback, FcMatchPattern);
      XftDefaultSubstitute(m_display, screen_num, fallback);
      FcDefaultSubstitute(fallback);

      FcPattern *fallback_match = FcFontMatch(nullptr, fallback, &result);
      if (fallback_match) {
        m_font = XftFontOpenPattern(m_display, fallback_match);
      }
      FcPatternDestroy(fallback);
    }

    if (m_font) {
      XGlyphInfo ext_m{}, ext_i{};
      XftTextExtentsUtf8(m_display, m_font, reinterpret_cast<const FcChar8 *>("M"), 1, &ext_m);
      XftTextExtentsUtf8(m_display, m_font, reinterpret_cast<const FcChar8 *>("i"), 1, &ext_i);
      if (ext_m.xOff > 0 && ext_m.xOff == ext_i.xOff) {
        m_is_monospace = true;
        m_cell_width = ext_m.xOff;
      } else {
        m_is_monospace = false;
        m_cell_width = ext_m.xOff > 0 ? ext_m.xOff : 8;
      }
    }
  }

public:
  /**
   * @brief Destructor. Closes Xft fonts and frees cached XftColors and XftDraw.
   */
  ~AntialiasedFont() {
    if (m_draw) {
      XftDrawDestroy(m_draw);
      m_draw = nullptr;
    }
    if (m_font && m_display) {
      XftFontClose(m_display, m_font);
      m_font = nullptr;
    }

    for (auto &pair : m_fallback_fonts) {
      if (pair.second && pair.second != m_font && m_display) {
        XftFontClose(m_display, pair.second);
      }
    }
    m_fallback_fonts.clear();

    /* Free all cached allocated colors */
    if (m_display && m_visual && m_colormap) {
      for (auto &pair : m_allocated_colors) {
        XftColorFree(m_display, m_visual, m_colormap, &pair.second);
      }
    }
    m_allocated_colors.clear();
  }

  AntialiasedFont(const AntialiasedFont &) = delete;
  AntialiasedFont &operator=(const AntialiasedFont &) = delete;

  AntialiasedFont(AntialiasedFont &&other) noexcept
      : m_display(other.m_display),
        m_visual(other.m_visual),
        m_colormap(other.m_colormap),
        m_font(other.m_font),
        m_draw(other.m_draw),
        m_current_drawable(other.m_current_drawable),
        m_allocated_colors(std::move(other.m_allocated_colors)),
        m_fallback_fonts(std::move(other.m_fallback_fonts)),
        m_is_monospace(other.m_is_monospace),
        m_cell_width(other.m_cell_width),
        m_ligaturesEnabled(other.m_ligaturesEnabled) {
    other.m_font = nullptr;
    other.m_draw = nullptr;
    other.m_current_drawable = 0;
  }

  AntialiasedFont &operator=(AntialiasedFont &&other) noexcept {
    if (this != &other) {
      if (m_draw) {
        XftDrawDestroy(m_draw);
      }
      if (m_font && m_display) {
        XftFontClose(m_display, m_font);
      }
      for (auto &pair : m_fallback_fonts) {
        if (pair.second && pair.second != m_font && m_display) {
          XftFontClose(m_display, pair.second);
        }
      }
      m_fallback_fonts.clear();

      if (m_display && m_visual && m_colormap) {
        for (auto &pair : m_allocated_colors) {
          XftColorFree(m_display, m_visual, m_colormap, &pair.second);
        }
      }
      m_allocated_colors.clear();

      m_display = other.m_display;
      m_visual = other.m_visual;
      m_colormap = other.m_colormap;
      m_font = other.m_font;
      m_draw = other.m_draw;
      m_current_drawable = other.m_current_drawable;
      m_allocated_colors = std::move(other.m_allocated_colors);
      m_fallback_fonts = std::move(other.m_fallback_fonts);
      m_is_monospace = other.m_is_monospace;
      m_cell_width = other.m_cell_width;
      m_ligaturesEnabled = other.m_ligaturesEnabled;

      other.m_font = nullptr;
      other.m_draw = nullptr;
      other.m_current_drawable = 0;
    }
    return *this;
  }

  bool isValid() const noexcept { return m_font != nullptr; }

  void setLigaturesEnabled(bool enabled) { m_ligaturesEnabled = enabled; }
  bool isLigaturesEnabled() const { return m_ligaturesEnabled; }

  static inline bool is_ascii_only(std::string_view text) noexcept {
    for (char c : text) {
      if (static_cast<unsigned char>(c) >= 0x80) return false;
    }
    return true;
  }

  static inline FcChar32 next_utf8_char(const char *&ptr, const char *end) {
    if (ptr >= end) return 0;
    unsigned char c = static_cast<unsigned char>(*ptr++);
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
      if (ptr >= end) return c;
      unsigned char c2 = static_cast<unsigned char>(*ptr++);
      return ((c & 0x1F) << 6) | (c2 & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
      if (ptr + 1 >= end) { ptr = end; return c; }
      unsigned char c2 = static_cast<unsigned char>(*ptr++);
      unsigned char c3 = static_cast<unsigned char>(*ptr++);
      return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
      if (ptr + 2 >= end) { ptr = end; return c; }
      unsigned char c2 = static_cast<unsigned char>(*ptr++);
      unsigned char c3 = static_cast<unsigned char>(*ptr++);
      unsigned char c4 = static_cast<unsigned char>(*ptr++);
      return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }
    return c;
  }

  static inline int unicode_column_width(FcChar32 ucs4) {
    if (ucs4 == 0) return 0;
    if (ucs4 < 0x80) return 1;
    if (ucs4 < 0x20 || (ucs4 >= 0x7F && ucs4 < 0xA0)) return 0;
    if ((ucs4 >= 0x0300 && ucs4 <= 0x036F) || (ucs4 >= 0x200B && ucs4 <= 0x200F)) return 0;
    if ((ucs4 >= 0x1100 && ucs4 <= 0x115F) ||
        (ucs4 >= 0x2E80 && ucs4 <= 0xA4CF) ||
        (ucs4 >= 0xAC00 && ucs4 <= 0xD7A3) ||
        (ucs4 >= 0xF900 && ucs4 <= 0xFAFF) ||
        (ucs4 >= 0xFE10 && ucs4 <= 0xFE19) ||
        (ucs4 >= 0xFE30 && ucs4 <= 0xFE6F) ||
        (ucs4 >= 0xFF01 && ucs4 <= 0xFF60) ||
        (ucs4 >= 0xFFE0 && ucs4 <= 0xFFE6) ||
        (ucs4 >= 0x1F300 && ucs4 <= 0x1FAFF)) {
      return 2;
    }
    return 1;
  }

  XftFont* getFallbackFont(FcChar32 ucs4) {
    auto it = m_fallback_fonts.find(ucs4);
    if (it != m_fallback_fonts.end()) {
      return it->second;
    }
    if (!m_display) {
      return nullptr;
    }

    FcCharSet *charset = FcCharSetCreate();
    FcCharSetAddChar(charset, ucs4);

    FcPattern *pattern = FcPatternCreate();
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    if (m_font && m_font->pattern) {
      double pixel_size = 14.0;
      if (FcPatternGetDouble(m_font->pattern, FC_PIXEL_SIZE, 0, &pixel_size) == FcResultMatch) {
        FcPatternAddDouble(pattern, FC_PIXEL_SIZE, pixel_size);
      }
    }
    FcPatternAddBool(pattern, FC_ANTIALIAS, FcTrue);
    FcPatternAddBool(pattern, FC_HINTING, FcTrue);
    FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_SLIGHT);

    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    XftDefaultSubstitute(m_display, DefaultScreen(m_display), pattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    XftFont *fallback_font = nullptr;
    if (match) {
      fallback_font = XftFontOpenPattern(m_display, match);
      if (!fallback_font) {
        // XftFontOpenPattern did not take ownership on failure
        FcPatternDestroy(match);
      }
    }
    FcPatternDestroy(pattern);
    FcCharSetDestroy(charset);

    m_fallback_fonts[ucs4] = fallback_font;
    return fallback_font;
  }

  /**
   * @brief Draw a UTF-8 string on a drawable with automatic fallback font support.
   */
  void drawString(Drawable drawable, const std::string &color_name, int x,
                  int y, std::string_view text, const XRectangle *clip = nullptr) {
    if (text.empty() || !m_font || !m_display || drawable == 0) {
      return;
    }

    XftColor *color = getColor(color_name);
    if (!color) {
      return;
    }

    if (!m_draw || m_current_drawable != drawable) {
      if (m_draw) {
        XftDrawDestroy(m_draw);
        m_draw = nullptr;
      }
      m_draw = XftDrawCreate(m_display, drawable, m_visual, m_colormap);
      m_current_drawable = drawable;
    }

    if (m_draw) {
      if (clip) {
        XftDrawSetClipRectangles(m_draw, 0, 0, clip, 1);
      } else {
        XftDrawSetClip(m_draw, nullptr);
      }

      // Fast path: pure ASCII text never needs fallback (vast majority of source code)
      if (is_ascii_only(text)) {
        XftDrawStringUtf8(m_draw, color, m_font, x, y,
                          reinterpret_cast<const FcChar8 *>(text.data()),
                          static_cast<int>(text.size()));
        return;
      }

      // Check if all non-ASCII characters exist in primary font
      const char *p = text.data();
      const char *end = p + text.size();
      bool has_missing = false;
      while (p < end) {
        FcChar32 ucs4 = next_utf8_char(p, end);
        if (ucs4 > 127 && !XftCharExists(m_display, m_font, ucs4)) {
          has_missing = true;
          break;
        }
      }

      if (!has_missing) {
        XftDrawStringUtf8(m_draw, color, m_font, x, y,
                          reinterpret_cast<const FcChar8 *>(text.data()),
                          static_cast<int>(text.size()));
        return;
      }

      // Fallback path: render runs using font fallback
      if (m_is_monospace) {
        p = text.data();
        int col = 0;
        while (p < end) {
          const char *chunk_start = p;
          const int chunk_col_start = col;
          FcChar32 first_ucs4 = next_utf8_char(p, end);
          col += unicode_column_width(first_ucs4);

          XftFont *cur_font = m_font;
          if (!XftCharExists(m_display, m_font, first_ucs4)) {
            XftFont *fb = getFallbackFont(first_ucs4);
            if (fb) cur_font = fb;
          }

          if (cur_font == m_font) {
            const char *chunk_end = p;
            while (p < end) {
              const char *saved_p = p;
              FcChar32 ucs4 = next_utf8_char(p, end);
              if (!XftCharExists(m_display, m_font, ucs4)) {
                p = saved_p;
                break;
              }
              col += unicode_column_width(ucs4);
              chunk_end = p;
            }
            const int chunk_len = static_cast<int>(chunk_end - chunk_start);
            XftDrawStringUtf8(m_draw, color, m_font,
                              x + chunk_col_start * m_cell_width, y,
                              reinterpret_cast<const FcChar8 *>(chunk_start),
                              chunk_len);
          } else {
            const int chunk_len = static_cast<int>(p - chunk_start);
            XftDrawStringUtf8(m_draw, color, cur_font,
                              x + chunk_col_start * m_cell_width, y,
                              reinterpret_cast<const FcChar8 *>(chunk_start),
                              chunk_len);
          }
        }
        return;
      }

      p = text.data();
      int cur_x = x;
      while (p < end) {
        const char *chunk_start = p;
        FcChar32 first_ucs4 = next_utf8_char(p, end);
        XftFont *cur_font = m_font;
        if (!XftCharExists(m_display, m_font, first_ucs4)) {
          XftFont *fb = getFallbackFont(first_ucs4);
          if (fb) cur_font = fb;
        }

        const char *chunk_end = p;
        while (p < end) {
          const char *saved_p = p;
          FcChar32 ucs4 = next_utf8_char(p, end);
          XftFont *match_font = m_font;
          if (!XftCharExists(m_display, m_font, ucs4)) {
            XftFont *fb = getFallbackFont(ucs4);
            if (fb) match_font = fb;
          }
          if (match_font != cur_font) {
            p = saved_p;
            break;
          }
          chunk_end = p;
        }

        const int chunk_len = static_cast<int>(chunk_end - chunk_start);
        XftDrawStringUtf8(m_draw, color, cur_font, cur_x, y,
                          reinterpret_cast<const FcChar8 *>(chunk_start),
                          chunk_len);

        XGlyphInfo extents{};
        XftTextExtentsUtf8(m_display, cur_font,
                           reinterpret_cast<const FcChar8 *>(chunk_start),
                           chunk_len, &extents);
        cur_x += extents.xOff;
      }
    }
  }

  void resetDrawable() noexcept {
    if (m_draw) {
      XftDrawDestroy(m_draw);
      m_draw = nullptr;
      m_current_drawable = 0;
    }
  }

  /* Font metrics for positioning and centering */
  int getAscent() const { return m_font ? m_font->ascent : 0; }
  int getDescent() const { return m_font ? m_font->descent : 0; }
  int getHeight() const {
    return m_font ? (m_font->ascent + m_font->descent) : 0;
  }

  /**
   * @brief Calculate the exact pixel width of a UTF-8 text string.
   */
  int getTextWidth(std::string_view text) const {
    if (!m_font || text.empty() || !m_display) {
      return 0;
    }

    if (m_is_monospace) {
      // Fast path: ASCII text in monospace = trivial multiplication
      if (is_ascii_only(text)) {
        return static_cast<int>(text.size()) * m_cell_width;
      }
      const char *p = text.data();
      const char *end = p + text.size();
      int cols = 0;
      while (p < end) {
        FcChar32 ucs4 = next_utf8_char(p, end);
        cols += unicode_column_width(ucs4);
      }
      return cols * m_cell_width;
    }

    // Fast path: ASCII text never has missing glyphs
    if (is_ascii_only(text)) {
      XGlyphInfo extents{};
      XftTextExtentsUtf8(m_display, m_font,
                         reinterpret_cast<const FcChar8 *>(text.data()),
                         static_cast<int>(text.size()), &extents);
      return extents.xOff;
    }

    const char *p = text.data();
    const char *end = p + text.size();
    bool has_missing = false;
    while (p < end) {
      FcChar32 ucs4 = next_utf8_char(p, end);
      if (ucs4 > 127 && !XftCharExists(m_display, m_font, ucs4)) {
        has_missing = true;
        break;
      }
    }

    if (!has_missing) {
      XGlyphInfo extents{};
      XftTextExtentsUtf8(m_display, m_font,
                         reinterpret_cast<const FcChar8 *>(text.data()),
                         static_cast<int>(text.size()), &extents);
      return extents.xOff;
    }

    p = text.data();
    int total_width = 0;
    while (p < end) {
      const char *chunk_start = p;
      FcChar32 first_ucs4 = next_utf8_char(p, end);
      XftFont *cur_font = m_font;
      if (!XftCharExists(m_display, m_font, first_ucs4)) {
        XftFont *fb = const_cast<AntialiasedFont*>(this)->getFallbackFont(first_ucs4);
        if (fb) cur_font = fb;
      }

      const char *chunk_end = p;
      while (p < end) {
        const char *saved_p = p;
        FcChar32 ucs4 = next_utf8_char(p, end);
        XftFont *match_font = m_font;
        if (!XftCharExists(m_display, m_font, ucs4)) {
          XftFont *fb = const_cast<AntialiasedFont*>(this)->getFallbackFont(ucs4);
          if (fb) match_font = fb;
        }
        if (match_font != cur_font) {
          p = saved_p;
          break;
        }
        chunk_end = p;
      }

      const int chunk_len = static_cast<int>(chunk_end - chunk_start);
      XGlyphInfo extents{};
      XftTextExtentsUtf8(m_display, cur_font,
                         reinterpret_cast<const FcChar8 *>(chunk_start),
                         chunk_len, &extents);
      total_width += extents.xOff;
    }
    return total_width;
  }

private:
  /**
   * @brief Internally allocate and cache XftColors with full 16-bit RGB precision.
   */
  XftColor *getColor(const std::string &color_name) {
    auto it = m_allocated_colors.find(color_name);
    if (it != m_allocated_colors.end()) {
      return &it->second;
    }

    if (color_name.empty() || !m_display || !m_visual) {
      return nullptr;
    }

    // Parse hex colors directly with high precision
    if (color_name[0] == '#') {
      unsigned int r = 255, g = 255, b = 255, a = 255;
      const std::size_t len = color_name.length();
      if (len == 7) { // #rrggbb
        if (std::sscanf(color_name.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
          a = 255;
        }
      } else if (len == 9) { // #rrggbbaa
        std::sscanf(color_name.c_str(), "#%02x%02x%02x%02x", &r, &g, &b, &a);
      } else if (len == 4) { // #rgb
        unsigned int sr = 0, sg = 0, sb = 0;
        if (std::sscanf(color_name.c_str(), "#%1x%1x%1x", &sr, &sg, &sb) == 3) {
          r = sr * 17;
          g = sg * 17;
          b = sb * 17;
          a = 255;
        }
      } else if (len == 5) { // #rgba
        unsigned int sr = 0, sg = 0, sb = 0, sa = 0;
        if (std::sscanf(color_name.c_str(), "#%1x%1x%1x%1x", &sr, &sg, &sb, &sa) == 4) {
          r = sr * 17;
          g = sg * 17;
          b = sb * 17;
          a = sa * 17;
        }
      }

      XRenderColor render_color{};
      render_color.red   = static_cast<unsigned short>((r * 65535U) / 255U);
      render_color.green = static_cast<unsigned short>((g * 65535U) / 255U);
      render_color.blue  = static_cast<unsigned short>((b * 65535U) / 255U);
      render_color.alpha = static_cast<unsigned short>((a * 65535U) / 255U);

      XftColor color{};
      if (XftColorAllocValue(m_display, m_visual, m_colormap, &render_color, &color)) {
        m_allocated_colors[color_name] = color;
        return &m_allocated_colors[color_name];
      }
    }

    // Named color fallback
    XftColor color{};
    if (XftColorAllocName(m_display, m_visual, m_colormap, color_name.c_str(), &color)) {
      m_allocated_colors[color_name] = color;
      return &m_allocated_colors[color_name];
    }

    std::cerr << "Warning: Failed to allocate XftColor '" << color_name << "'"
              << std::endl;
    return nullptr;
  }

  Display *m_display = nullptr;
  Visual *m_visual = nullptr;
  Colormap m_colormap = 0;
  XftFont *m_font = nullptr;
  XftDraw *m_draw = nullptr;
  Drawable m_current_drawable = 0;
  std::unordered_map<std::string, XftColor> m_allocated_colors;
  std::unordered_map<FcChar32, XftFont*> m_fallback_fonts;
  bool m_is_monospace = false;
  int m_cell_width = 8;
  bool m_ligaturesEnabled = false;
};

#endif /* defined(_WIN32) || defined(__unix__) */

#if defined(__APPLE__)

#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>

/**
 * @class AntialiasedFont
 * @brief Helper class for high-quality anti-aliased font rendering on macOS
 * using CoreText / CoreGraphics.
 *
 * Provides the same API surface as the Win32 (GDI) and X11 (Xft) versions so
 * that platform-specific Components can call drawString / getTextWidth /
 * getAscent / getDescent / getHeight uniformly.
 */
class AntialiasedFont {
public:
  /**
   * @brief Constructor. Opens a font by family name and pixel size.
   * @param font_family  Font family name (e.g. "Menlo", "Hack", "SF Pro").
   * @param pixel_size   Desired pixel size.
   * @param bold         Use bold weight.
   */
  AntialiasedFont(const std::string &font_family, float pixel_size,
                  bool bold = false) {
    CFStringRef family_ref =
        CFStringCreateWithCString(nullptr, font_family.c_str(),
                                  kCFStringEncodingUTF8);
    if (!family_ref) {
      return;
    }

    CTFontSymbolicTraits traits = bold ? kCTFontBoldTrait : 0;
    m_font = CTFontCreateWithName(family_ref, static_cast<CGFloat>(pixel_size),
                                  nullptr);
    CFRelease(family_ref);
    if (!m_font) {
      // Fallback to system font if specified family is not found
      m_font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, static_cast<CGFloat>(pixel_size), nullptr);
      if (!m_font) {
          m_font = CTFontCreateWithName(CFSTR("Helvetica"), static_cast<CGFloat>(pixel_size), nullptr);
      }
      if (!m_font) {
          return;
      }
    }

    if (bold) {
      CTFontRef bold_font =
          CTFontCreateCopyWithSymbolicTraits(m_font, 0.0, nullptr, traits,
                                            traits);
      if (bold_font) {
        CFRelease(m_font);
        m_font = bold_font;
      }
    }

    m_ascent = static_cast<int>(std::ceil(CTFontGetAscent(m_font)));
    m_descent = static_cast<int>(std::ceil(CTFontGetDescent(m_font)));
  }

  ~AntialiasedFont() {
    if (m_font) {
      CFRelease(m_font);
    }
  }

  AntialiasedFont(const AntialiasedFont &) = delete;
  AntialiasedFont &operator=(const AntialiasedFont &) = delete;

  bool isValid() const noexcept { return m_font != nullptr; }

  void setLigaturesEnabled(bool enabled) { m_ligaturesEnabled = enabled; }
  bool isLigaturesEnabled() const { return m_ligaturesEnabled; }

  /* Font metrics --------------------------------------------------------- */

  int getAscent() const { return m_ascent; }
  int getDescent() const { return m_descent; }
  int getHeight() const { return m_ascent + m_descent; }

  /**
   * @brief Calculate the exact pixel width of a UTF-8 text string.
   */
  int getTextWidth(const std::string &text) const {
    if (!m_font || text.empty()) {
      return 0;
    }
    CFStringRef str = CFStringCreateWithCString(nullptr, text.c_str(),
                                                kCFStringEncodingUTF8);
    if (!str) {
      return 0;
    }
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        nullptr, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontAttributeName, m_font);
    CFAttributedStringRef attr_str =
        CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attr_str);
    double width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFRelease(line);
    CFRelease(attr_str);
    CFRelease(attrs);
    CFRelease(str);
    return static_cast<int>(std::ceil(width));
  }

  /**
   * @brief Draw a UTF-8 string into a CGContext.
   * @param context   The CoreGraphics context to draw into.
   * @param color_hex Color as hex string (e.g. "#3b82f6") or named color.
   * @param x         X coordinate (baseline origin).
   * @param y         Y coordinate (baseline origin, flipped for CG).
   * @param text      UTF-8 text to draw.
   * @param clip      Optional clip rect (in CG coordinates, already flipped).
   */
  void drawString(CGContextRef context, const std::string &color_hex, int x,
                  int y, const std::string &text,
                  const CGRect *clip = nullptr) {
    if (!m_font || !context || text.empty()) {
      return;
    }

    CGFloat r = 1.0, g = 1.0, b = 1.0, a = 1.0;
    parseHexColor(color_hex, r, g, b, a);

    CGColorRef cg_color = CGColorCreateSRGB(r, g, b, a);

    CFStringRef str = CFStringCreateWithCString(nullptr, text.c_str(),
                                                kCFStringEncodingUTF8);
    if (!str) {
      CGColorRelease(cg_color);
      return;
    }

    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
        nullptr, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontAttributeName, m_font);
    CFDictionarySetValue(attrs, kCTForegroundColorAttributeName, cg_color);

    CFAttributedStringRef attr_str =
        CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attr_str);

    CGContextSaveGState(context);
    if (clip) {
      CGContextClipToRect(context, *clip);
    }
    
    // In a flipped NSView, CoreText draws upside down unless we flip the text matrix.
    CGContextSetTextMatrix(context, CGAffineTransformMakeScale(1.0, -1.0));
    
    CGContextSetTextPosition(context, static_cast<CGFloat>(x),
                             static_cast<CGFloat>(y));
    CTLineDraw(line, context);
    CGContextRestoreGState(context);

    CFRelease(line);
    CFRelease(attr_str);
    CFRelease(attrs);
    CFRelease(str);
    CGColorRelease(cg_color);
  }

  CTFontRef getCTFont() const { return m_font; }

private:
  static void parseHexColor(const std::string &hex, CGFloat &r, CGFloat &g,
                            CGFloat &b, CGFloat &a) {
    a = 1.0;
    if (hex.empty()) {
      r = g = b = 1.0;
      return;
    }
    if (hex[0] == '#') {
      if (hex.size() >= 9) {
        unsigned int rv = 0, gv = 0, bv = 0, av = 255;
        std::sscanf(hex.c_str(), "#%02x%02x%02x%02x", &rv, &gv, &bv, &av);
        r = static_cast<CGFloat>(rv) / 255.0;
        g = static_cast<CGFloat>(gv) / 255.0;
        b = static_cast<CGFloat>(bv) / 255.0;
        a = static_cast<CGFloat>(av) / 255.0;
      } else if (hex.size() >= 7) {
        unsigned int rv = 0, gv = 0, bv = 0;
        std::sscanf(hex.c_str(), "#%02x%02x%02x", &rv, &gv, &bv);
        r = static_cast<CGFloat>(rv) / 255.0;
        g = static_cast<CGFloat>(gv) / 255.0;
        b = static_cast<CGFloat>(bv) / 255.0;
      } else if (hex.size() == 5) {
        unsigned int rv = 0, gv = 0, bv = 0, av = 15;
        std::sscanf(hex.c_str(), "#%1x%1x%1x%1x", &rv, &gv, &bv, &av);
        r = static_cast<CGFloat>(rv * 17) / 255.0;
        g = static_cast<CGFloat>(gv * 17) / 255.0;
        b = static_cast<CGFloat>(bv * 17) / 255.0;
        a = static_cast<CGFloat>(av * 17) / 255.0;
      } else if (hex.size() == 4) {
        unsigned int rv = 0, gv = 0, bv = 0;
        std::sscanf(hex.c_str(), "#%1x%1x%1x", &rv, &gv, &bv);
        r = static_cast<CGFloat>(rv * 17) / 255.0;
        g = static_cast<CGFloat>(gv * 17) / 255.0;
        b = static_cast<CGFloat>(bv * 17) / 255.0;
      } else {
        r = g = b = 1.0;
      }
    } else if (hex == "white") {
      r = g = b = 1.0;
    } else if (hex == "black") {
      r = g = b = 0.0;
    } else {
      r = g = b = 1.0;
    }
  }

  CTFontRef m_font = nullptr;
  int m_ascent = 0;
  int m_descent = 0;
  bool m_ligaturesEnabled = false;
};

#endif /* defined(__APPLE__) */

#endif /* FONTS_H */
