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

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), NULL, 0);
    if (wlen > 0) {
        std::wstring wtext(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &wtext[0], wlen);

        if (m_ligaturesEnabled) {
            SCRIPT_STRING_ANALYSIS ssa = nullptr;
            HRESULT hr = ScriptStringAnalyse(hdc, wtext.c_str(), static_cast<int>(wtext.length()), static_cast<int>(wtext.length() * 3 / 2 + 16), -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
            if (SUCCEEDED(hr)) {
                ScriptStringOut(ssa, x, y - tm.tmAscent, 0, NULL, 0, 0, FALSE);
                ScriptStringFree(&ssa);
            } else {
                TextOutW(hdc, x, y - tm.tmAscent, wtext.c_str(), static_cast<int>(wtext.length()));
            }
        } else {
            TextOutW(hdc, x, y - tm.tmAscent, wtext.c_str(), static_cast<int>(wtext.length()));
        }
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

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), NULL, 0);
    if (wlen > 0) {
        std::wstring wtext(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &wtext[0], wlen);

        if (m_ligaturesEnabled) {
            SCRIPT_STRING_ANALYSIS ssa = nullptr;
            HRESULT hr = ScriptStringAnalyse(hdc, wtext.c_str(), static_cast<int>(wtext.length()), static_cast<int>(wtext.length() * 3 / 2 + 16), -1, SSA_GLYPHS | SSA_FALLBACK | SSA_LINK, 0, NULL, NULL, NULL, NULL, NULL, &ssa);
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
            GetTextExtentPoint32W(hdc, wtext.c_str(), static_cast<int>(wtext.length()), &size);
            width = size.cx;
        }
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
    init(screen_num, font_name);
  }

  /**
   * @brief Constructor with custom visual and colormap (e.g. for 32-bit ARGB windows).
   */
  AntialiasedFont(Display *display, int screen_num,
                  const std::string &font_name,
                  Visual* visual, Colormap colormap)
      : m_display(display), m_visual(visual), m_colormap(colormap), m_font(nullptr) {
    init(screen_num, font_name);
  }

private:
  void init(int screen_num, const std::string &font_name) {
    /* Open the vector font using Fontconfig pattern matching */
    m_font = XftFontOpenName(m_display, screen_num, font_name.c_str());
    if (!m_font) {
      std::cerr << "Warning: Could not open font '" << font_name
                << "', falling back to 'sans-10'" << std::endl;
      m_font = XftFontOpenName(m_display, screen_num, "sans-10");
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
                  int y, const std::string &text, const XRectangle *clip = nullptr) {
    XftColor *color = getColor(color_name);
    if (!color || !m_font)
      return;

    if (!m_draw) {
      m_draw = XftDrawCreate(m_display, drawable, m_visual, m_colormap);
    } else {
      XftDrawChange(m_draw, drawable);
    }
    if (m_draw) {
      if (clip) {
        XftDrawSetClipRectangles(m_draw, 0, 0, clip, 1);
      } else {
        XftDrawSetClip(m_draw, nullptr);
      }
      XftDrawStringUtf8(m_draw, color, m_font, x, y,
                        (const FcChar8 *)text.c_str(), text.length());
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
  XftDraw *m_draw = nullptr;
  std::unordered_map<std::string, XftColor> m_allocated_colors;
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
    CFRange range = CFRangeMake(0, CFStringGetLength(str));
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
    if (hex.size() >= 7 && hex[0] == '#') {
      unsigned int rv = 0, gv = 0, bv = 0;
      std::sscanf(hex.c_str(), "#%02x%02x%02x", &rv, &gv, &bv);
      r = static_cast<CGFloat>(rv) / 255.0;
      g = static_cast<CGFloat>(gv) / 255.0;
      b = static_cast<CGFloat>(bv) / 255.0;
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
