#pragma once

#include "UI/Geometry.h"
#include "Utility/Ascii/AsciiArtConverter.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

namespace Zenvra::Utility::Ascii
{

class AsciiMascotRenderer
{
public:
#ifdef _WIN32
    // Render ASCII art directly into a Win32 HDC within bounds
    static void render_ascii_to_dc(
        HDC dc,
        const AsciiArtResult& art,
        const UI::Rect& bounds,
        COLORREF bg_color = RGB(20, 21, 24),
        bool colored = true,
        HFONT custom_font = nullptr);

    // Create a cached HBITMAP with pre-rendered ASCII art
    static HBITMAP create_ascii_thumbnail_bitmap(
        HDC dc,
        const std::string& image_path,
        int width,
        int height,
        COLORREF bg_color = RGB(20, 21, 24));

    // High level helper to render mascot image as solid or ASCII
    static bool render_mascot_ascii(
        HDC dc,
        const std::string& image_path,
        const UI::Rect& bounds,
        COLORREF bg_color = RGB(20, 21, 24),
        float dpi_scale = 1.0F);

    // Clear internal bitmap cache
    static void clear_bitmap_cache();
#endif
#ifndef _WIN32
    // No-op stub on non-Windows so cross-platform UI code (e.g.
    // SettingsControl) can call this unconditionally without #ifdef.
    static void clear_bitmap_cache() {}
#endif
};

} // namespace Zenvra::Utility::Ascii
