#ifndef FONTS_H
#define FONTS_H

#include <iostream>
#include <string>
#include <unordered_map>


#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <Usp10.h> // Uniscribe for ligatures

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
  AntialiasedFont(const std::string &font_name, int font_size = 16) {
    // CLEARTYPE_QUALITY ensures sub-pixel anti-aliasing on Windows
    m_font = CreateFontA(-font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_SWISS, font_name.c_str());

    if (!m_font) {
      std::cerr << "Warning: Could not open font '" << font_name << "'"
                << std::endl;
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
    if (!m_font || !hdc)
      return;

    COLORREF color = parseColor(color_name);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    // GDI TextOut draws from the top-left, while Xft draws from the baseline.
    // Adjust Y by subtracting the ascent to match Xft's baseline behavior.
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);

    if (m_ligaturesEnabled && text.length() > 0) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), NULL, 0);
        if (wlen > 0) {
            std::wstring wtext(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &wtext[0], wlen);

            SCRIPT_STRING_ANALYSIS ssa = nullptr;
            HRESULT hr = ScriptStringAnalyse(hdc, wtext.c_str(), wtext.length(), wtext.length() * 3 / 2 + 16, -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
            if (SUCCEEDED(hr)) {
                ScriptStringOut(ssa, x, y - tm.tmAscent, 0, NULL, 0, 0, FALSE);
                ScriptStringFree(&ssa);
            } else {
                TextOutA(hdc, x, y - tm.tmAscent, text.c_str(), static_cast<int>(text.length()));
            }
        }
    } else {
        TextOutA(hdc, x, y - tm.tmAscent, text.c_str(), text.length());
    }

    SelectObject(hdc, oldFont);
  }

  int getAscent(HDC hdc) const {
    TEXTMETRIC tm;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, oldFont);
    return tm.tmAscent;
  }

  int getDescent(HDC hdc) const {
    TEXTMETRIC tm;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, oldFont);
    return tm.tmDescent;
  }

  int getHeight(HDC hdc) const {
    TEXTMETRIC tm;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, oldFont);
    return tm.tmHeight;
  }

  int getTextWidth(HDC hdc, const std::string &text) const {
    if (!m_font || !hdc || text.empty())
      return 0;
      
    int width = 0;
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

    if (m_ligaturesEnabled) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), NULL, 0);
        if (wlen > 0) {
            std::wstring wtext(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &wtext[0], wlen);

            SCRIPT_STRING_ANALYSIS ssa = nullptr;
            HRESULT hr = ScriptStringAnalyse(hdc, wtext.c_str(), wtext.length(), wtext.length() * 3 / 2 + 16, -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
            if (SUCCEEDED(hr)) {
                const SIZE* pSize = ScriptString_pSize(ssa);
                if (pSize) {
                    width = pSize->cx;
                }
                ScriptStringFree(&ssa);
            }
        }
    }

    if (width == 0) {
        SIZE size;
        GetTextExtentPoint32A(hdc, text.c_str(), static_cast<int>(text.length()), &size);
        width = size.cx;
    }

    SelectObject(hdc, oldFont);
    return width;
  }

private:
  HFONT m_font;
  bool m_ligaturesEnabled = false;

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

#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>


/**
 * @class AntialiasedFont
 * @brief Helper class to handle high-quality anti-aliased font rendering in raw
 * Xlib using Xft.
 *
 * Traditional Xlib core fonts (XLoadFont, XDrawString) are pixel-based and look
 * jagged. This class wraps Xft (X FreeType) and Fontconfig to render modern
 * anti-aliased vector fonts (TrueType/OpenType) with sub-pixel quality on X11
 * drawables.
 */
class AntialiasedFont {
public:
  /**
   * @brief Constructor. Opens a vector font by description.
   * @param display Connection to the X server.
   * @param screen_num Screen index.
   * @param font_name Font name string in Fontconfig format (e.g. "DejaVu
   * Sans-10:bold", "sans-11").
   */
  AntialiasedFont(Display *display, int screen_num,
                  const std::string &font_name)
      : m_display(display), m_font(nullptr) {
    m_visual = DefaultVisual(display, screen_num);
    m_colormap = DefaultColormap(display, screen_num);

    /* Open the vector font using Fontconfig pattern matching */
    m_font = XftFontOpenName(display, screen_num, font_name.c_str());
    if (!m_font) {
      std::cerr << "Warning: Could not open font '" << font_name
                << "', falling back to 'sans-10'" << std::endl;
      m_font = XftFontOpenName(display, screen_num, "sans-10");
    }
  }

  /**
   * @brief Destructor. Closes Xft fonts and frees cached XftColors.
   */
  ~AntialiasedFont() {
    if (m_font) {
      XftFontClose(m_display, m_font);
    }

    /* Free all cached allocated colors */
    for (auto &pair : m_allocated_colors) {
      XftColorFree(m_display, m_visual, m_colormap, &pair.second);
    }
  }

  bool isValid() const noexcept { return m_font != nullptr; }

  void setLigaturesEnabled(bool enabled) { m_ligaturesEnabled = enabled; }
  bool isLigaturesEnabled() const { return m_ligaturesEnabled; }

  /**
   * @brief Draw an UTF-8 string on a drawable.
   * @param drawable Window or Pixmap target.
   * @param color_name Color name or hex value (e.g. "black", "#3b82f6").
   * @param x X coordinate (baseline origin).
   * @param y Y coordinate (baseline origin).
   * @param text String content to draw.
   */
  void drawString(Drawable drawable, const std::string &color_name, int x,
                  int y, const std::string &text) {
    XftColor *color = getColor(color_name);
    if (!color || !m_font)
      return;

    XftDraw *draw = XftDrawCreate(m_display, drawable, m_visual, m_colormap);
    if (draw) {
      XftDrawStringUtf8(draw, color, m_font, x, y,
                        (const FcChar8 *)text.c_str(), text.length());
      XftDrawDestroy(draw);
    }
  }

  /* Font metrics for positioning and centering */
  int getAscent() const { return m_font ? m_font->ascent : 0; }
  int getDescent() const { return m_font ? m_font->descent : 0; }
  int getHeight() const {
    return m_font ? (m_font->ascent + m_font->descent) : 0;
  }

  /**
   * @brief Calculate the exact pixel width of a text string.
   * @param text Target string.
   * @return Width in pixels.
   */
  int getTextWidth(const std::string &text) const {
    if (!m_font)
      return 0;
    XGlyphInfo extents;
    XftTextExtentsUtf8(m_display, m_font, (const FcChar8 *)text.c_str(),
                       text.length(), &extents);
    return extents.xOff;
  }

private:
  /**
   * @brief Internally allocate and cache XftColors.
   */
  XftColor *getColor(const std::string &color_name) {
    auto it = m_allocated_colors.find(color_name);
    if (it != m_allocated_colors.end()) {
      return &it->second;
    }

    XftColor color;
    if (XftColorAllocName(m_display, m_visual, m_colormap, color_name.c_str(),
                          &color)) {
      m_allocated_colors[color_name] = color;
      return &m_allocated_colors[color_name];
    }

    std::cerr << "Warning: Failed to allocate XftColor '" << color_name << "'"
              << std::endl;
    return nullptr;
  }

  Display *m_display;
  Visual *m_visual;
  Colormap m_colormap;
  XftFont *m_font;
  std::unordered_map<std::string, XftColor> m_allocated_colors;
  bool m_ligaturesEnabled = false;
};

#endif /* defined(_WIN32) || defined(__unix__) */

#endif /* FONTS_H */
