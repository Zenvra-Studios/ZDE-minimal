#pragma once
#include <X11/Xlib.h>
#include <cmath>
#include <algorithm>

namespace Zenvra::Utility::X11Rounded {

class X11Rounded {
public:
    /**
     * @brief Draws a filled rounded rectangle using standard X11 drawing primitives.
     * @param r The corner radius. If r >= w/2 or r >= h/2, it becomes pill-shaped.
     */
    static void fillRoundedRect(Display* display, Drawable d, GC gc, int x, int y, int w, int h, int r) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) {
            XFillRectangle(display, d, gc, x, y, w, h);
            return;
        }
        
        // Clamp radius to prevent overlapping
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        
        // Draw the main body using 3 rectangles
        XFillRectangle(display, d, gc, x + r, y, w - 2 * r, r);         // Top edge
        XFillRectangle(display, d, gc, x, y + r, w, h - 2 * r);         // Center block
        XFillRectangle(display, d, gc, x + r, y + h - r, w - 2 * r, r); // Bottom edge
        
        // Draw the 4 corner arcs
        // Angles are in units of 1/64th of a degree. Positive is counter-clockwise.
        // 0 degrees is 3 o'clock.
        XFillArc(display, d, gc, x, y, r * 2, r * 2, 90 * 64, 90 * 64);                 // Top-left
        XFillArc(display, d, gc, x + w - 2 * r, y, r * 2, r * 2, 0 * 64, 90 * 64);      // Top-right
        XFillArc(display, d, gc, x, y + h - 2 * r, r * 2, r * 2, 180 * 64, 90 * 64);    // Bottom-left
        XFillArc(display, d, gc, x + w - 2 * r, y + h - 2 * r, r * 2, r * 2, 270 * 64, 90 * 64); // Bottom-right
    }

    /**
     * @brief Draws a filled anti-aliased rounded rectangle using standard X11 drawing primitives.
     */
    static void fillRoundedRectAA(Display* display, Drawable d, GC gc, int x, int y, int w, int h, int r, unsigned long fg_color, unsigned long bg_color, bool is_32bit) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) {
            XSetForeground(display, gc, fg_color);
            XFillRectangle(display, d, gc, x, y, w, h);
            return;
        }

        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;

        XSetForeground(display, gc, fg_color);
        XFillRectangle(display, d, gc, x + r, y, w - 2 * r, r);         // Top edge
        XFillRectangle(display, d, gc, x, y + r, w, h - 2 * r);         // Center block
        XFillRectangle(display, d, gc, x + r, y + h - r, w - 2 * r, r); // Bottom edge

        const unsigned int fg_r = (fg_color >> 16) & 0xFF;
        const unsigned int fg_g = (fg_color >> 8) & 0xFF;
        const unsigned int fg_b = fg_color & 0xFF;
        const unsigned int fg_a = is_32bit ? ((fg_color >> 24) & 0xFF) : 255;
        const unsigned int actual_fg_a = fg_a == 0 ? 255 : fg_a;

        const unsigned int bg_r = (bg_color >> 16) & 0xFF;
        const unsigned int bg_g = (bg_color >> 8) & 0xFF;
        const unsigned int bg_b = bg_color & 0xFF;
        const unsigned int bg_a = is_32bit ? ((bg_color >> 24) & 0xFF) : 255;

        auto blend = [&](int px, int py, int dx, int dy) {
            float dist = std::sqrt(static_cast<float>((dx + 0.5f - r) * (dx + 0.5f - r) + (dy + 0.5f - r) * (dy + 0.5f - r)));
            float alpha = std::max(0.0f, std::min(1.0f, r + 0.5f - dist));
            if (alpha <= 0.0f) return;
            if (alpha >= 1.0f) {
                XSetForeground(display, gc, fg_color);
                XDrawPoint(display, d, gc, px, py);
                return;
            }
            
            unsigned long out_color;
            if (is_32bit && bg_a == 0) {
                // Background is transparent
                unsigned int a = static_cast<unsigned int>(actual_fg_a * alpha);
                // Pre-multiply RGB with alpha for XRender compositing
                unsigned int r_out = static_cast<unsigned int>(fg_r * alpha);
                unsigned int g_out = static_cast<unsigned int>(fg_g * alpha);
                unsigned int b_out = static_cast<unsigned int>(fg_b * alpha);
                out_color = (a << 24) | (r_out << 16) | (g_out << 8) | b_out;
            } else {
                unsigned int out_r = static_cast<unsigned int>(fg_r * alpha + bg_r * (1.0f - alpha));
                unsigned int out_g = static_cast<unsigned int>(fg_g * alpha + bg_g * (1.0f - alpha));
                unsigned int out_b = static_cast<unsigned int>(fg_b * alpha + bg_b * (1.0f - alpha));
                out_color = (out_r << 16) | (out_g << 8) | out_b;
                if (is_32bit) out_color |= (255UL << 24);
            }
            
            XSetForeground(display, gc, out_color);
            XDrawPoint(display, d, gc, px, py);
        };

        for (int dy = 0; dy < r; ++dy) {
            for (int dx = 0; dx < r; ++dx) {
                blend(x + dx, y + dy, dx, dy);
                blend(x + w - 1 - dx, y + dy, dx, dy);
                blend(x + dx, y + h - 1 - dy, dx, dy);
                blend(x + w - 1 - dx, y + h - 1 - dy, dx, dy);
            }
        }
    }

    /**
     * @brief Draws the outline of a rounded rectangle using standard X11 drawing primitives.
     * @param display The connection to the X server.
     * @param d The drawable (window or pixmap) to draw on.
     * @param gc The graphics context to use for drawing.
     * @param x The x-coordinate of the top-left corner.
     * @param y The y-coordinate of the top-left corner.
     * @param w The width of the rectangle.
     * @param h The height of the rectangle.
     * @param r The corner radius. If r >= w/2 or r >= h/2, it becomes pill-shaped.
     */
    static void drawRoundedRect(Display* display, Drawable d, GC gc, int x, int y, int w, int h, int r) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) {
            XDrawRectangle(display, d, gc, x, y, w - 1, h - 1);
            return;
        }
        
        // Clamp radius to prevent overlapping
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        
        // 4 lines
        XDrawLine(display, d, gc, x + r, y, x + w - r - 1, y);                 // Top
        XDrawLine(display, d, gc, x + r, y + h - 1, x + w - r - 1, y + h - 1); // Bottom
        XDrawLine(display, d, gc, x, y + r, x, y + h - r - 1);                 // Left
        XDrawLine(display, d, gc, x + w - 1, y + r, x + w - 1, y + h - r - 1); // Right
        
        // 4 corners
        XDrawArc(display, d, gc, x, y, r * 2, r * 2, 90 * 64, 90 * 64);                 // Top-left
        XDrawArc(display, d, gc, x + w - 2 * r - 1, y, r * 2, r * 2, 0 * 64, 90 * 64);      // Top-right
        XDrawArc(display, d, gc, x, y + h - 2 * r - 1, r * 2, r * 2, 180 * 64, 90 * 64);    // Bottom-left
        XDrawArc(display, d, gc, x + w - 2 * r - 1, y + h - 2 * r - 1, r * 2, r * 2, 270 * 64, 90 * 64); // Bottom-right
    }
};

} // namespace ui