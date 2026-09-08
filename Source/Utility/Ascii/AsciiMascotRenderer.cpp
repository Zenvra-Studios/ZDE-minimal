#include "Utility/Ascii/AsciiMascotRenderer.h"

#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Zenvra::Utility::Ascii
{

namespace
{

struct CachedAsciiBitmap
{
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;
};

std::mutex g_bmp_cache_mutex;
std::unordered_map<std::string, CachedAsciiBitmap> g_bmp_cache;

} // namespace

void AsciiMascotRenderer::render_ascii_to_dc(
    HDC dc,
    const AsciiArtResult& art,
    const UI::Rect& bounds,
    COLORREF bg_color,
    bool colored,
    HFONT custom_font)
{
    if (!dc || !art.is_valid() || bounds.width <= 0.0F || bounds.height <= 0.0F) {
        return;
    }

    const float cell_w = bounds.width / static_cast<float>(art.width);
    const float cell_h = bounds.height / static_cast<float>(art.height);

    HFONT local_font = custom_font;
    bool created_font = false;

    if (!local_font) {
        const int font_h = std::max(7, static_cast<int>(std::round(cell_h)));
        const int font_w = std::max(1, static_cast<int>(std::round(cell_w)));
        local_font = CreateFontW(
            -font_h, font_w, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        created_font = (local_font != nullptr);
    }

    const int prev_bk_mode = SetBkMode(dc, TRANSPARENT);
    const COLORREF prev_text_color = GetTextColor(dc);
    HGDIOBJ prev_font = local_font ? SelectObject(dc, local_font) : nullptr;

    const uint8_t bg_r = GetRValue(bg_color);
    const uint8_t bg_g = GetGValue(bg_color);
    const uint8_t bg_b = GetBValue(bg_color);

    for (int y = 0; y < art.height; ++y) {
        const int py = static_cast<int>(std::round(bounds.y + static_cast<float>(y) * cell_h));
        for (int x = 0; x < art.width; ++x) {
            const auto* cell = art.cell_at(x, y);
            if (!cell || cell->character == ' ' || cell->a < 12) {
                continue;
            }

            const int px = static_cast<int>(std::round(bounds.x + static_cast<float>(x) * cell_w));

            if (colored) {
                // Adaptive clarity boost: lifts faint line contours while maintaining solid gradient dynamics
                const float lum_val = (0.2126F * cell->r + 0.7152F * cell->g + 0.0722F * cell->b) / 255.0F;
                const float boost_factor = 1.0F + 0.85F * (1.0F - lum_val);
                int r = std::clamp(static_cast<int>(cell->r * boost_factor + 45.0F), 0, 255);
                int g = std::clamp(static_cast<int>(cell->g * boost_factor + 45.0F), 0, 255);
                int b = std::clamp(static_cast<int>(cell->b * boost_factor + 45.0F), 0, 255);

                const uint8_t out_r = static_cast<uint8_t>((r * cell->a + bg_r * (255 - cell->a)) / 255);
                const uint8_t out_g = static_cast<uint8_t>((g * cell->a + bg_g * (255 - cell->a)) / 255);
                const uint8_t out_b = static_cast<uint8_t>((b * cell->a + bg_b * (255 - cell->a)) / 255);
                SetTextColor(dc, RGB(out_r, out_g, out_b));
            } else {
                SetTextColor(dc, RGB(245, 248, 255));
            }

            const WCHAR ch = static_cast<WCHAR>(static_cast<unsigned char>(cell->character));
            TextOutW(dc, px, py, &ch, 1);
        }
    }

    if (prev_font) {
        SelectObject(dc, prev_font);
    }
    if (created_font && local_font) {
        DeleteObject(local_font);
    }

    SetTextColor(dc, prev_text_color);
    SetBkMode(dc, prev_bk_mode);
}

HBITMAP AsciiMascotRenderer::create_ascii_thumbnail_bitmap(
    HDC dc,
    const std::string& image_path,
    int width,
    int height,
    COLORREF bg_color)
{
    if (width <= 0 || height <= 0 || image_path.empty()) {
        return nullptr;
    }

    const std::string cache_key = image_path + "@ascii_thumb_v4#" +
                                  std::to_string(width) + "x" +
                                  std::to_string(height) + "/" +
                                  std::to_string(bg_color);

    {
        std::lock_guard<std::mutex> lock(g_bmp_cache_mutex);
        const auto it = g_bmp_cache.find(cache_key);
        if (it != g_bmp_cache.end() && it->second.bitmap != nullptr) {
            return it->second.bitmap;
        }
    }

    // Render at 2x supersampling for ultra sharp thumbnail downscaling
    const int super_w = width * 2;
    const int super_h = height * 2;

    AsciiConvertOptions opts;
    opts.target_width = 40;
    opts.target_height = 20;
    opts.char_aspect = 0.5F;
    opts.ramp = AsciiRamp::Detailed;
    opts.colored = true;
    opts.alpha_threshold = 16;

    const auto art = AsciiArtConverter::convert_image_file(image_path, opts);
    if (!art.is_valid()) {
        return nullptr;
    }

    HDC mem_dc = CreateCompatibleDC(dc);
    if (!mem_dc) {
        return nullptr;
    }

    HBITMAP super_bm = CreateCompatibleBitmap(dc ? dc : mem_dc, super_w, super_h);
    if (!super_bm) {
        DeleteDC(mem_dc);
        return nullptr;
    }

    HGDIOBJ prev_super = SelectObject(mem_dc, super_bm);
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    RECT fill_super = { 0, 0, super_w, super_h };
    FillRect(mem_dc, &fill_super, bg_brush);
    DeleteObject(bg_brush);

    render_ascii_to_dc(mem_dc, art, UI::Rect{ 0.0F, 0.0F, static_cast<float>(super_w), static_cast<float>(super_h) }, bg_color, true);

    // Create target bitmap and downscale smoothly with HALFTONE
    HDC target_dc = CreateCompatibleDC(dc);
    HBITMAP target_bm = CreateCompatibleBitmap(dc ? dc : mem_dc, width, height);
    HGDIOBJ prev_target = SelectObject(target_dc, target_bm);

    SetStretchBltMode(target_dc, HALFTONE);
    SetBrushOrgEx(target_dc, 0, 0, nullptr);
    StretchBlt(target_dc, 0, 0, width, height, mem_dc, 0, 0, super_w, super_h, SRCCOPY);

    SelectObject(target_dc, prev_target);
    DeleteDC(target_dc);

    SelectObject(mem_dc, prev_super);
    DeleteObject(super_bm);
    DeleteDC(mem_dc);

    {
        std::lock_guard<std::mutex> lock(g_bmp_cache_mutex);
        g_bmp_cache[cache_key] = CachedAsciiBitmap{ target_bm, width, height };
    }

    return target_bm;
}

bool AsciiMascotRenderer::render_mascot_ascii(
    HDC dc,
    const std::string& image_path,
    const UI::Rect& bounds,
    COLORREF bg_color,
    float dpi_scale)
{
    if (!dc || bounds.width <= 0.0F || bounds.height <= 0.0F || image_path.empty()) {
        return false;
    }

    const float effective_scale = std::max(0.5F, dpi_scale);
    AsciiConvertOptions opts;
    // Column density aligned with font cell metrics (~3.8px cell width at 1x) to eliminate glyph overlap
    const int desired_cols = static_cast<int>(std::round(bounds.width / (3.8F * effective_scale)));
    opts.target_width = std::clamp(desired_cols, 50, 80);
    opts.target_height = 0; // auto-computed from image aspect ratio * char_aspect
    opts.char_aspect = 0.5F;
    opts.ramp = AsciiRamp::Detailed; // 68 levels of fine gradation
    opts.colored = true;
    opts.alpha_threshold = 16;

    const auto art = AsciiArtConverter::convert_image_file(image_path, opts);
    if (!art.is_valid()) {
        return false;
    }

    render_ascii_to_dc(dc, art, bounds, bg_color, true);
    return true;
}

void AsciiMascotRenderer::clear_bitmap_cache()
{
    std::lock_guard<std::mutex> lock(g_bmp_cache_mutex);
    for (auto& pair : g_bmp_cache) {
        if (pair.second.bitmap) {
            DeleteObject(pair.second.bitmap);
        }
    }
    g_bmp_cache.clear();
}

} // namespace Zenvra::Utility::Ascii
#endif
