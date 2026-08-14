#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#ifndef CLEARTYPE_NATURAL_QUALITY
#define CLEARTYPE_NATURAL_QUALITY 6
#endif
#endif

namespace Zenvra::Utility {

/**
 * @struct ColorRGBA
 * @brief Simple 8-bit RGBA color structure for image resampling and blending.
 */
struct ColorRGBA {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/**
 * @class AntialiasedImage
 * @brief High-quality anti-aliased image resampler and compositor.
 * Uses area-averaging supersampling for sharp, artifact-free downscaling
 * (e.g. 800x800 down to 40x40) and bilinear filtering for smooth scaling.
 */
class AntialiasedImage {
public:
    /**
     * @brief Resamples an RGBA source buffer to target dimensions using area-averaging.
     * @param src_data Pointer to source RGBA pixels (4 bytes per pixel).
     * @param src_w Source image width in pixels.
     * @param src_h Source image height in pixels.
     * @param dst_w Destination width in pixels.
     * @param dst_h Destination height in pixels.
     * @return Resampled RGBA pixel vector of size dst_w * dst_h * 4.
     */
    static std::vector<std::uint8_t> resample_area_average(
        const std::uint8_t* src_data,
        int src_w,
        int src_h,
        int dst_w,
        int dst_h)
    {
        if (!src_data || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
            return {};
        }

        std::vector<std::uint8_t> dst_data(static_cast<std::size_t>(dst_w * dst_h * 4), 0);

        const float scale_x = static_cast<float>(src_w) / static_cast<float>(dst_w);
        const float scale_y = static_cast<float>(src_h) / static_cast<float>(dst_h);

        for (int dy = 0; dy < dst_h; ++dy) {
            const float src_y0 = static_cast<float>(dy) * scale_y;
            const float src_y1 = static_cast<float>(dy + 1) * scale_y;

            const int iy0 = static_cast<int>(std::floor(src_y0));
            const int iy1 = std::min(static_cast<int>(std::ceil(src_y1)), src_h);

            for (int dx = 0; dx < dst_w; ++dx) {
                const float src_x0 = static_cast<float>(dx) * scale_x;
                const float src_x1 = static_cast<float>(dx + 1) * scale_x;

                const int ix0 = static_cast<int>(std::floor(src_x0));
                const int ix1 = std::min(static_cast<int>(std::ceil(src_x1)), src_w);

                float total_weight = 0.0F;
                float accum_r = 0.0F;
                float accum_g = 0.0F;
                float accum_b = 0.0F;
                float accum_a = 0.0F;

                for (int sy = iy0; sy < iy1; ++sy) {
                    const float top = std::max(src_y0, static_cast<float>(sy));
                    const float bottom = std::min(src_y1, static_cast<float>(sy + 1));
                    const float h_weight = std::max(0.0F, bottom - top);

                    for (int sx = ix0; sx < ix1; ++sx) {
                        const float left = std::max(src_x0, static_cast<float>(sx));
                        const float right = std::min(src_x1, static_cast<float>(sx + 1));
                        const float w_weight = std::max(0.0F, right - left);

                        const float weight = h_weight * w_weight;
                        if (weight <= 0.0F) continue;

                        const std::size_t src_idx = static_cast<std::size_t>((sy * src_w + sx) * 4);
                        const float sa = static_cast<float>(src_data[src_idx + 3]) / 255.0F;
                        const float sr = static_cast<float>(src_data[src_idx]);
                        const float sg = static_cast<float>(src_data[src_idx + 1]);
                        const float sb = static_cast<float>(src_data[src_idx + 2]);

                        // Premultiplied alpha accumulation for smooth anti-aliased edges
                        accum_r += sr * sa * weight;
                        accum_g += sg * sa * weight;
                        accum_b += sb * sa * weight;
                        accum_a += sa * weight;
                        total_weight += weight;
                    }
                }

                const std::size_t dst_idx = static_cast<std::size_t>((dy * dst_w + dx) * 4);
                if (total_weight > 0.0F && accum_a > 0.0001F) {
                    const float final_a = accum_a / total_weight;
                    const float inv_a = 1.0F / accum_a;
                    dst_data[dst_idx]     = static_cast<std::uint8_t>(std::clamp(accum_r * inv_a, 0.0F, 255.0F));
                    dst_data[dst_idx + 1] = static_cast<std::uint8_t>(std::clamp(accum_g * inv_a, 0.0F, 255.0F));
                    dst_data[dst_idx + 2] = static_cast<std::uint8_t>(std::clamp(accum_b * inv_a, 0.0F, 255.0F));
                    dst_data[dst_idx + 3] = static_cast<std::uint8_t>(std::clamp(final_a * 255.0F, 0.0F, 255.0F));
                } else {
                    dst_data[dst_idx]     = 0;
                    dst_data[dst_idx + 1] = 0;
                    dst_data[dst_idx + 2] = 0;
                    dst_data[dst_idx + 3] = 0;
                }
            }
        }

        return dst_data;
    }

    /**
     * @brief Composites a resampled RGBA image onto a solid background and generates a 32-bit BGRA DIB buffer.
     * @param rgba_data RGBA image data from resample_area_average.
     * @param img_w Image width.
     * @param img_h Image height.
     * @param canvas_size Target square canvas size (max_size x max_size).
     * @param bg Background color for alpha compositing.
     * @return 32-bit BGRA pixel vector for GDI SetDIBitsToDevice.
     */
    static std::vector<std::uint32_t> composite_to_dib(
        const std::vector<std::uint8_t>& rgba_data,
        int img_w,
        int img_h,
        int canvas_size,
        const ColorRGBA& bg)
    {
        if (canvas_size <= 0 || rgba_data.empty()) {
            return {};
        }

        std::vector<std::uint32_t> dib(static_cast<std::size_t>(canvas_size * canvas_size));
        const std::uint32_t bg_pixel = (static_cast<std::uint32_t>(bg.r) << 16) |
                                      (static_cast<std::uint32_t>(bg.g) << 8) |
                                       static_cast<std::uint32_t>(bg.b);

        const int pad_x = (canvas_size - img_w) / 2;
        const int pad_y = (canvas_size - img_h) / 2;

        for (int y = 0; y < canvas_size; ++y) {
            // DIB bitmap is bottom-up (y=0 is bottom row)
            const int img_y = canvas_size - 1 - y;

            for (int x = 0; x < canvas_size; ++x) {
                if (x >= pad_x && x < pad_x + img_w && img_y >= pad_y && img_y < pad_y + img_h) {
                    const int sx = x - pad_x;
                    const int sy = img_y - pad_y;
                    const std::size_t src_idx = static_cast<std::size_t>((sy * img_w + sx) * 4);

                    const std::uint32_t sr = rgba_data[src_idx];
                    const std::uint32_t sg = rgba_data[src_idx + 1];
                    const std::uint32_t sb = rgba_data[src_idx + 2];
                    const std::uint32_t sa = rgba_data[src_idx + 3];

                    if (sa == 255) {
                        dib[static_cast<std::size_t>(y * canvas_size + x)] = (sr << 16) | (sg << 8) | sb;
                    } else if (sa == 0) {
                        dib[static_cast<std::size_t>(y * canvas_size + x)] = bg_pixel;
                    } else {
                        // High quality subpixel blending
                        const std::uint32_t inv_a = 255 - sa;
                        const std::uint32_t out_r = (sr * sa + static_cast<std::uint32_t>(bg.r) * inv_a + 127) / 255;
                        const std::uint32_t out_g = (sg * sa + static_cast<std::uint32_t>(bg.g) * inv_a + 127) / 255;
                        const std::uint32_t out_b = (sb * sa + static_cast<std::uint32_t>(bg.b) * inv_a + 127) / 255;
                        dib[static_cast<std::size_t>(y * canvas_size + x)] = (out_r << 16) | (out_g << 8) | out_b;
                    }
                } else {
                    dib[static_cast<std::size_t>(y * canvas_size + x)] = bg_pixel;
                }
            }
        }

        return dib;
    }
};

/**
 * @class AntialiasedText
 * @brief Utility for creating crisp, subpixel antialiased ClearType fonts on Win32.
 */
class AntialiasedText {
public:
#if defined(_WIN32) || defined(_WIN64)
    /**
     * @brief Creates a Win32 HFONT with CLEARTYPE_NATURAL_QUALITY and TrueType precision.
     */
    static HFONT create_cleartype_font(
        const std::wstring& family_name,
        int pixel_height,
        int font_weight = FW_NORMAL,
        bool italic = false)
    {
        HFONT font = CreateFontW(
            -pixel_height, 0, 0, 0,
            font_weight,
            italic ? TRUE : FALSE,
            FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_NATURAL_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            family_name.c_str());

        if (!font) {
            font = CreateFontW(
                -pixel_height, 0, 0, 0,
                font_weight,
                italic ? TRUE : FALSE,
                FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                family_name.c_str());
        }

        return font;
    }

    /**
     * @brief Renders antialiased text with subpixel clarity.
     */
    static void draw_text(
        HDC hdc,
        const std::wstring& text,
        const RECT& rect,
        COLORREF text_color,
        UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX)
    {
        if (!hdc || text.empty()) return;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text_color);
        RECT r = rect;
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.length()), &r, format);
    }
#endif
};

} // namespace Zenvra::Utility
