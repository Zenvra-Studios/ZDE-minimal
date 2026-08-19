#include "Platform/Cocoa/Components/ShaderSandboxPanel.h"
#include "Platform/Cocoa/Components/StudioWorkspaceRenderer.h"
#include "Services/Shader/ShaderCompiler.h"
#include "Utility/Fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Zenvra::Platform::Cocoa::Components
{

namespace
{

int round_to_int(float value)
{
    return static_cast<int>(std::lround(value));
}

constexpr CGFloat canvas_bg[4] = { 18.0 / 255.0, 18.0 / 255.0, 24.0 / 255.0, 1.0 };
constexpr CGFloat error_banner_bg[4] = { 40.0 / 255.0, 15.0 / 255.0, 18.0 / 255.0, 240.0 / 255.0 };
constexpr CGFloat error_border_color[4] = { 220.0 / 255.0, 60.0 / 255.0, 60.0 / 255.0, 1.0 };
constexpr CGFloat error_dot_color[4] = { 220.0 / 255.0, 50.0 / 255.0, 50.0 / 255.0, 1.0 };

} // namespace

ShaderSandboxPanel::ShaderSandboxPanel() = default;
ShaderSandboxPanel::~ShaderSandboxPanel() = default;

bool ShaderSandboxPanel::initialize()
{
    m_engine.initialize();
    return true;
}

bool ShaderSandboxPanel::toggle() noexcept
{
    m_visible = !m_visible;
    return m_visible;
}

void ShaderSandboxPanel::set_visible(bool visible) noexcept
{
    m_visible = visible;
}

bool ShaderSandboxPanel::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!m_visible || layout.shader_panel_bounds.is_empty())
    {
        return false;
    }
    const float scale = layout.dpi_scale;
    const float grab_margin = 6.0F * scale;
    const float splitter_x = layout.shader_panel_bounds.x;
    return point_x >= (splitter_x - grab_margin) &&
           point_x <= (splitter_x + grab_margin) &&
           point_y >= layout.shader_panel_bounds.y &&
           point_y <= layout.shader_panel_bounds.bottom();
}

bool ShaderSandboxPanel::contains(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept
{
    if (!m_visible)
    {
        return false;
    }
    return layout.shader_panel_bounds.contains(point_x, point_y) ||
           is_resize_handle_point(layout, point_x, point_y);
}

bool ShaderSandboxPanel::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y)
{
    if (!m_visible)
    {
        return false;
    }

    if (is_resize_handle_point(layout, point_x, point_y))
    {
        m_is_resizing = true;
        m_drag_start_x = point_x;
        m_drag_start_width = m_width;
        m_prev_scale = m_engine.get_resolution_scale();
        m_engine.set_resolution_scale(Services::Shader::ResolutionScale::Half);
        return true;
    }

    // Viewport mouse interaction (iMouse)
    if (layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        m_viewport_mouse_down = true;
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_engine.set_mouse(vx, vy, true);
        return true;
    }

    // Header buttons (use member bounds set during render)
    if (m_header_close_bounds.contains(point_x, point_y))
    {
        m_visible = false;
        return true;
    }

    // Preset selector button in header
    if (m_header_preset_bounds.contains(point_x, point_y))
    {
        next_preset();
        return true;
    }

    // Controls toolbar buttons (use member bounds set during render)
    if (m_ctrl_play_bounds.contains(point_x, point_y))
    {
        m_engine.toggle_playback();
        return true;
    }

    if (m_ctrl_reset_bounds.contains(point_x, point_y))
    {
        m_engine.reset_time();
        return true;
    }

    if (m_ctrl_scale_bounds.contains(point_x, point_y))
    {
        m_engine.cycle_resolution_scale();
        return true;
    }

    if (m_ctrl_backend_bounds.contains(point_x, point_y))
    {
        m_engine.toggle_render_backend();
        return true;
    }

    if (m_ctrl_snapshot_bounds.contains(point_x, point_y))
    {
        static_cast<void>(m_engine.export_snapshot_bmp("shader_artwork.bmp"));
        return true;
    }

    return false;
}

bool ShaderSandboxPanel::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    if (!m_visible)
    {
        return false;
    }

    const bool prev_close = m_hover_close;
    const bool prev_preset = m_hover_preset;
    const bool prev_play = m_hover_play;
    const bool prev_reset = m_hover_reset;
    const bool prev_scale = m_hover_scale;
    const bool prev_backend = m_hover_backend;
    const bool prev_snapshot = m_hover_snapshot;
    const bool prev_splitter = m_hover_splitter;

    m_hover_splitter = is_resize_handle_point(layout, point_x, point_y);

    m_hover_close = m_header_close_bounds.contains(point_x, point_y);
    m_hover_preset = m_header_preset_bounds.contains(point_x, point_y);

    // Use member bounds set during render for perfect sync
    m_hover_play = m_ctrl_play_bounds.contains(point_x, point_y);
    m_hover_reset = m_ctrl_reset_bounds.contains(point_x, point_y);
    m_hover_scale = m_ctrl_scale_bounds.contains(point_x, point_y);
    m_hover_backend = m_ctrl_backend_bounds.contains(point_x, point_y);
    m_hover_snapshot = m_ctrl_snapshot_bounds.contains(point_x, point_y);

    if (m_viewport_mouse_down && layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_engine.set_mouse(vx, vy, true);
    }

    return (prev_close != m_hover_close) || (prev_preset != m_hover_preset) ||
           (prev_play != m_hover_play) || (prev_reset != m_hover_reset) ||
           (prev_scale != m_hover_scale) || (prev_backend != m_hover_backend) ||
           (prev_snapshot != m_hover_snapshot) ||
           (prev_splitter != m_hover_splitter);
}

bool ShaderSandboxPanel::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept
{
    if (m_is_resizing)
    {
        const float delta = m_drag_start_x - point_x;
        const float scale = layout.dpi_scale;
        m_width = std::clamp(m_drag_start_width + delta, 180.0F * scale, 800.0F * scale);
        static_cast<void>(m_engine.update_and_render());
        return true;
    }

    if (m_viewport_mouse_down && layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_engine.set_mouse(vx, vy, true);
        static_cast<void>(m_engine.update_and_render());
        return true;
    }

    return false;
}

bool ShaderSandboxPanel::handle_pointer_release() noexcept
{
    const bool was_resizing = m_is_resizing;
    const bool was_mouse_down = m_viewport_mouse_down;
    if (m_is_resizing)
    {
        m_is_resizing = false;
        m_engine.set_resolution_scale(m_prev_scale);
    }
    if (m_viewport_mouse_down)
    {
        m_viewport_mouse_down = false;
        m_engine.set_mouse(0.0F, 0.0F, false);
    }
    return was_resizing || was_mouse_down;
}

bool ShaderSandboxPanel::tick_animations() noexcept
{
    if (!m_visible)
    {
        return false;
    }
    return m_engine.update_and_render();
}

void ShaderSandboxPanel::set_source_code(std::string_view source_code)
{
    m_engine.set_source_code(source_code);
}

void ShaderSandboxPanel::next_preset()
{
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    if (presets.empty())
    {
        return;
    }
    const std::size_t next_idx = (m_engine.get_active_preset_index() + 1) % presets.size();
    m_engine.load_preset(next_idx);
}

void ShaderSandboxPanel::previous_preset()
{
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    if (presets.empty())
    {
        return;
    }
    const std::size_t prev_idx = (m_engine.get_active_preset_index() + presets.size() - 1) % presets.size();
    m_engine.load_preset(prev_idx);
}

void ShaderSandboxPanel::render(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!m_visible || layout.shader_panel_bounds.is_empty() || context == nullptr)
    {
        return;
    }

    // Draw background
    surface.fill_rectangle(context, layout.shader_panel_bounds, surface.m_colors.sidebar_background);

    render_header(surface, context, layout);
    render_viewport(surface, context, layout);
    render_controls(surface, context, layout);

    // Draw Splitter border on the left edge with blue accent highlight when hovered or resizing
    const bool show_accent = m_hover_splitter || m_is_resizing;
    const CGFloat* splitter_color = show_accent
        ? surface.m_colors.accent
        : surface.m_colors.border;

    const float splitter_x = layout.shader_panel_bounds.x;
    surface.draw_line(context,
        round_to_int(splitter_x),
        round_to_int(layout.shader_panel_bounds.y),
        round_to_int(splitter_x),
        round_to_int(layout.shader_panel_bounds.bottom()),
        splitter_color);

    if (show_accent)
    {
        surface.fill_rectangle(context,
            UI::Rect{
                splitter_x - surface.m_dpi_scale,
                layout.shader_panel_bounds.y,
                std::max(2.0F * surface.m_dpi_scale, 2.0F),
                layout.shader_panel_bounds.height},
            surface.m_colors.accent);
    }
}

void ShaderSandboxPanel::render_header(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& header = layout.shader_panel_header_bounds;
    const float scale = layout.dpi_scale;

    // Header background & bottom border
    surface.fill_rectangle(context, header, surface.m_colors.tab_background);
    surface.draw_line(context,
        round_to_int(header.x), round_to_int(header.bottom() - 1.0F),
        round_to_int(header.right()), round_to_int(header.bottom() - 1.0F),
        surface.m_colors.border);

    // Status Dot indicator (Green = Running, Yellow = Paused, Red = Error, Cyan = Compiling)
    const CGFloat* status_color = surface.m_colors.success;
    switch (m_engine.get_status())
    {
    case Services::Shader::ShaderStatus::Running:
        status_color = surface.m_colors.success;
        break;
    case Services::Shader::ShaderStatus::Paused:
        status_color = surface.m_colors.warning;
        break;
    case Services::Shader::ShaderStatus::Compiling:
        status_color = surface.m_colors.accent;
        break;
    case Services::Shader::ShaderStatus::Error:
        status_color = error_dot_color;
        break;
    case Services::Shader::ShaderStatus::Idle:
        status_color = surface.m_colors.text_muted;
        break;
    }

    const float dot_y = header.y + header.height * 0.5F;
    surface.fill_rounded_rectangle(context,
        UI::Rect{header.x + 12.0F * scale, dot_y - 3.5F * scale, 7.0F * scale, 7.0F * scale},
        status_color, 3.5F * scale);

    // Title text
    surface.draw_text(context, *surface.m_ui_font, "Shader Sandbox",
        header.x + 26.0F * scale, dot_y, surface.m_text.primary);

    // FPS badge text
    std::ostringstream fps_ss;
    fps_ss << std::fixed << std::setprecision(0) << m_engine.get_fps() << " FPS";
    surface.draw_text(context, *surface.m_small_font, fps_ss.str(),
        header.x + 130.0F * scale, dot_y, surface.m_text.muted);

    // Preset selector button (dynamic width)
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    std::string preset_name = "Presets";
    if (m_engine.get_active_preset_index() < presets.size())
    {
        preset_name = presets[m_engine.get_active_preset_index()].name;
    }
    const int text_w = surface.m_small_font
        ? surface.m_small_font->getTextWidth(preset_name)
        : round_to_int(80.0F * scale);
    const float preset_w = std::clamp(static_cast<float>(text_w) + 26.0F * scale,
                                      90.0F * scale, 160.0F * scale);

    // Close button (far right of header)
    const float close_btn_w = 22.0F * scale;
    const float close_btn_h = 22.0F * scale;
    m_header_close_bounds = UI::Rect{
        header.right() - close_btn_w - 6.0F * scale,
        header.y + (header.height - close_btn_h) * 0.5F,
        close_btn_w,
        close_btn_h};
    if (m_hover_close)
    {
        surface.fill_rounded_rectangle(context, m_header_close_bounds, surface.m_colors.hover_background, 4.0F * scale);
    }
    surface.draw_svg_icon(context, "Assets/icons/close-minimal.svg",
        round_to_int(m_header_close_bounds.x + m_header_close_bounds.width * 0.5F),
        round_to_int(m_header_close_bounds.y + m_header_close_bounds.height * 0.5F),
        std::max(round_to_int(10.0F * scale), 8),
        m_hover_close ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
        surface.m_palette.tab_background,
        false);

    // Preset button (positioned left of close)
    const float preset_x = m_header_close_bounds.x - preset_w - 6.0F * scale;
    m_header_preset_bounds = UI::Rect{
        preset_x,
        header.y + (header.height - 22.0F * scale) * 0.5F,
        preset_w,
        22.0F * scale};
    surface.fill_rounded_rectangle(context, m_header_preset_bounds,
        m_hover_preset ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_header_preset_bounds, surface.m_colors.border);
    surface.draw_text(context, *surface.m_small_font, preset_name,
        m_header_preset_bounds.x + 8.0F * scale, dot_y,
        m_hover_preset ? "#FFFFFF" : surface.m_text.primary, &m_header_preset_bounds);
    // Chevron indicator
    surface.draw_svg_icon(context, "Assets/icons/chevron-down.svg",
        round_to_int(m_header_preset_bounds.right() - 10.0F * scale),
        round_to_int(dot_y),
        std::max(round_to_int(8.0F * scale), 6),
        surface.m_palette.text_muted,
        surface.m_palette.sidebar_background,
        false);
}

void ShaderSandboxPanel::render_viewport(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& vp = layout.shader_panel_viewport_bounds;
    if (vp.is_empty())
    {
        return;
    }

    const float scale = layout.dpi_scale;
    const float margin = 8.0F * scale;
    const UI::Rect canvas_rect{
        vp.x + margin,
        vp.y + margin,
        std::max(vp.width - margin * 2.0F, 16.0F),
        std::max(vp.height - margin * 2.0F, 16.0F)};

    // Dark canvas frame & subtle inner background
    surface.fill_rectangle(context, canvas_rect, canvas_bg);
    surface.draw_rectangle(context, canvas_rect, surface.m_colors.border);

    // Resize engine rasterizer to canvas size if dimensions changed
    const int target_w = round_to_int(canvas_rect.width);
    const int target_h = round_to_int(canvas_rect.height);
    if (target_w > 16 && target_h > 16 &&
        (m_engine.get_viewport_width() != target_w || m_engine.get_viewport_height() != target_h))
    {
        const_cast<ShaderSandboxPanel*>(this)->m_engine.resize(target_w, target_h);
        static_cast<void>(const_cast<ShaderSandboxPanel*>(this)->m_engine.update_and_render());
    }

    // Blit rasterized pixels
    const auto pixels = m_engine.get_rendered_pixels();
    const int img_w = m_engine.get_rendered_width();
    const int img_h = m_engine.get_rendered_height();

    if (!pixels.empty() && img_w > 0 && img_h > 0)
    {
        std::vector<std::uint8_t> rgba_data(static_cast<std::size_t>(img_w * img_h * 4));
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const std::uint32_t p = pixels[i];
            rgba_data[i * 4 + 0] = static_cast<std::uint8_t>((p >> 16U) & 0xFFU);
            rgba_data[i * 4 + 1] = static_cast<std::uint8_t>((p >> 8U) & 0xFFU);
            rgba_data[i * 4 + 2] = static_cast<std::uint8_t>(p & 0xFFU);
            rgba_data[i * 4 + 3] = static_cast<std::uint8_t>((p >> 24U) & 0xFFU);
        }

        CGColorSpaceRef color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef bmp_ctx = CGBitmapContextCreate(
            rgba_data.data(),
            img_w, img_h, 8, img_w * 4, color_space,
            static_cast<uint32_t>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
        if (bmp_ctx)
        {
            CGImageRef img = CGBitmapContextCreateImage(bmp_ctx);
            if (img)
            {
                CGContextSaveGState(context);
                CGContextClipToRect(context, CGRectMake(canvas_rect.x, canvas_rect.y, canvas_rect.width, canvas_rect.height));
                CGContextTranslateCTM(context, canvas_rect.x, canvas_rect.y + canvas_rect.height);
                CGContextScaleCTM(context, 1.0, -1.0);
                CGContextDrawImage(context, CGRectMake(0, 0, canvas_rect.width, canvas_rect.height), img);
                CGContextRestoreGState(context);
                CGImageRelease(img);
            }
            CGContextRelease(bmp_ctx);
        }
        CGColorSpaceRelease(color_space);
    }

    // Error overlay banner if compilation failed
    render_diagnostics_overlay(surface, context, canvas_rect);
}

void ShaderSandboxPanel::render_controls(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& ctrl = layout.shader_panel_controls_bounds;
    const float scale = layout.dpi_scale;

    // Controls toolbar background & top border
    surface.fill_rectangle(context, ctrl, surface.m_colors.tab_background);
    surface.draw_line(context,
        round_to_int(ctrl.x), round_to_int(ctrl.y),
        round_to_int(ctrl.right()), round_to_int(ctrl.y),
        surface.m_colors.border);

    const float btn_w = 26.0F * scale;
    const float btn_h = 22.0F * scale;
    const float btn_y = ctrl.y + (ctrl.height - btn_h) * 0.5F;

    // Play/Pause button
    m_ctrl_play_bounds = UI::Rect{ctrl.x + 8.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(context, m_ctrl_play_bounds,
        m_hover_play ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_ctrl_play_bounds, surface.m_colors.border);
    {
        const float cx = m_ctrl_play_bounds.x + m_ctrl_play_bounds.width * 0.5F;
        const float cy = m_ctrl_play_bounds.y + m_ctrl_play_bounds.height * 0.5F;
        if (m_engine.is_playing())
        {
            // Elegant two vertical pause bars
            const float bar_w = 2.5F * scale;
            const float bar_h = 9.0F * scale;
            const float bar_gap = 2.0F * scale;
            const CGFloat* bar_color = m_hover_play
                ? surface.m_colors.accent
                : surface.m_colors.text_primary;
            surface.fill_rounded_rectangle(context,
                UI::Rect{cx - bar_gap - bar_w, cy - bar_h * 0.5F, bar_w, bar_h},
                bar_color, 1.0F * scale);
            surface.fill_rounded_rectangle(context,
                UI::Rect{cx + bar_gap, cy - bar_h * 0.5F, bar_w, bar_h},
                bar_color, 1.0F * scale);
        }
        else
        {
            surface.draw_svg_icon(context,
                "Assets/icons/play.svg",
                round_to_int(cx), round_to_int(cy),
                std::max(round_to_int(11.0F * scale), 9),
                m_hover_play ? surface.m_palette.accent : UI::Theme::Color{255, 255, 255, 255},
                surface.m_palette.tab_background,
                false);
        }
    }

    // Reset Time button
    m_ctrl_reset_bounds = UI::Rect{m_ctrl_play_bounds.right() + 4.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(context, m_ctrl_reset_bounds,
        m_hover_reset ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_ctrl_reset_bounds, surface.m_colors.border);
    surface.draw_svg_icon(context, "Assets/icons/refresh.svg",
        round_to_int(m_ctrl_reset_bounds.x + m_ctrl_reset_bounds.width * 0.5F),
        round_to_int(m_ctrl_reset_bounds.y + m_ctrl_reset_bounds.height * 0.5F),
        std::max(round_to_int(11.0F * scale), 9),
        m_hover_reset ? surface.m_palette.accent : surface.m_palette.text_muted,
        surface.m_palette.tab_background,
        false);

    // Resolution scale badge button (1x, 0.5x, 0.25x)
    m_ctrl_scale_bounds = UI::Rect{m_ctrl_reset_bounds.right() + 4.0F * scale, btn_y, 44.0F * scale, btn_h};
    surface.fill_rounded_rectangle(context, m_ctrl_scale_bounds,
        m_hover_scale ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_ctrl_scale_bounds, surface.m_colors.border);
    std::string scale_str = "1.0x";
    switch (m_engine.get_resolution_scale())
    {
    case Services::Shader::ResolutionScale::Full: scale_str = "1.0x"; break;
    case Services::Shader::ResolutionScale::Half: scale_str = "0.5x"; break;
    case Services::Shader::ResolutionScale::Quarter: scale_str = "0.25x"; break;
    }
    {
        const int sw = surface.m_small_font ? surface.m_small_font->getTextWidth(scale_str) : round_to_int(24.0F * scale);
        const float sx = m_ctrl_scale_bounds.x + (m_ctrl_scale_bounds.width - static_cast<float>(sw)) * 0.5F;
        surface.draw_text(context, *surface.m_small_font, scale_str,
            sx, btn_y + btn_h * 0.5F,
            m_hover_scale ? surface.m_text.accent : surface.m_text.muted,
            &m_ctrl_scale_bounds);
    }

    // Backend toggle badge button (CPU / GPU)
    m_ctrl_backend_bounds = UI::Rect{m_ctrl_scale_bounds.right() + 4.0F * scale, btn_y, 40.0F * scale, btn_h};
    const bool is_gpu = (m_engine.get_render_backend() == Services::Shader::RenderBackend::Gpu);
    surface.fill_rounded_rectangle(context, m_ctrl_backend_bounds,
        m_hover_backend ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_ctrl_backend_bounds, surface.m_colors.border);
    {
        const std::string backend_str = is_gpu ? "GPU" : "CPU";
        const int bw = surface.m_small_font ? surface.m_small_font->getTextWidth(backend_str) : round_to_int(20.0F * scale);
        const float bx = m_ctrl_backend_bounds.x + (m_ctrl_backend_bounds.width - static_cast<float>(bw)) * 0.5F;
        surface.draw_text(context, *surface.m_small_font, backend_str,
            bx, btn_y + btn_h * 0.5F,
            is_gpu ? std::string("#50DC8C") : (m_hover_backend ? surface.m_text.accent : surface.m_text.muted),
            &m_ctrl_backend_bounds);
    }

    // Time & Frame indicator
    std::ostringstream time_ss;
    time_ss << std::fixed << std::setprecision(1) << m_engine.get_time() << "s | f" << m_engine.get_frame();
    surface.draw_text(context, *surface.m_small_font, time_ss.str(),
        m_ctrl_backend_bounds.right() + 10.0F * scale, btn_y + btn_h * 0.5F, surface.m_text.muted);

    // Snapshot button
    m_ctrl_snapshot_bounds = UI::Rect{ctrl.right() - btn_w - 8.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(context, m_ctrl_snapshot_bounds,
        m_hover_snapshot ? surface.m_colors.hover_background : surface.m_colors.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(context, m_ctrl_snapshot_bounds, surface.m_colors.border);
    surface.draw_svg_icon(context, "Assets/icons/build.svg",
        round_to_int(m_ctrl_snapshot_bounds.x + m_ctrl_snapshot_bounds.width * 0.5F),
        round_to_int(m_ctrl_snapshot_bounds.y + m_ctrl_snapshot_bounds.height * 0.5F),
        std::max(round_to_int(11.0F * scale), 9),
        m_hover_snapshot ? surface.m_palette.accent : surface.m_palette.text_muted,
        surface.m_palette.tab_background,
        false);
}

void ShaderSandboxPanel::render_diagnostics_overlay(
    const StudioWorkspaceRenderer& surface,
    CGContextRef context,
    const UI::Rect& viewport_rect) const
{
    const auto& diagnostics = m_engine.get_diagnostics();
    if (diagnostics.empty() && m_engine.get_status() != Services::Shader::ShaderStatus::Error)
    {
        return;
    }

    const float scale = surface.m_dpi_scale;
    const float banner_height = 42.0F * scale;
    const UI::Rect banner_rect{
        viewport_rect.x,
        viewport_rect.bottom() - banner_height,
        viewport_rect.width,
        banner_height};

    // Dark semi-transparent error red overlay
    surface.fill_rectangle(context, banner_rect, error_banner_bg);
    surface.draw_line(context,
        round_to_int(banner_rect.x), round_to_int(banner_rect.y),
        round_to_int(banner_rect.right()), round_to_int(banner_rect.y),
        error_border_color);

    std::string err_msg = "Shader compilation error";
    if (!diagnostics.empty())
    {
        err_msg = "L" + std::to_string(diagnostics[0].line) + ": " + diagnostics[0].message;
    }
    surface.draw_text(context, *surface.m_small_font, err_msg,
        banner_rect.x + 8.0F * scale, banner_rect.y + banner_height * 0.5F,
        "#ff7878", &banner_rect);
}

} // namespace Zenvra::Platform::Cocoa::Components
