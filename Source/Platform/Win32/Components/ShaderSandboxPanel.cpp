#include "Platform/Win32/Components/ShaderSandboxPanel.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Services/Shader/ShaderCompiler.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace Zenvra::Platform::Win32::Components
{

namespace
{

using Zenvra::Utility::round_to_int;

} // namespace

ShaderSandboxPanel::ShaderSandboxPanel() = default;

ShaderSandboxPanel::~ShaderSandboxPanel() = default;

bool ShaderSandboxPanel::initialize()
{
    m_service.initialize();
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
        return true;
    }

    // Viewport mouse interaction (iMouse)
    if (layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        m_viewport_mouse_down = true;
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_service.set_mouse(vx, vy, true);
        return true;
    }

    // Header buttons
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
        m_service.toggle_playback();
        return true;
    }

    if (m_ctrl_reset_bounds.contains(point_x, point_y))
    {
        m_service.reset_time();
        return true;
    }

    if (m_ctrl_scale_bounds.contains(point_x, point_y))
    {
        m_service.cycle_resolution_scale();
        return true;
    }

    if (m_ctrl_backend_bounds.contains(point_x, point_y))
    {
        m_service.toggle_render_backend();
        return true;
    }

    if (m_ctrl_snapshot_bounds.contains(point_x, point_y))
    {
        static_cast<void>(m_service.export_snapshot_bmp("shader_artwork.bmp"));
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

    if (m_viewport_mouse_down &&
        layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_service.set_mouse(vx, vy, true);
    }

    return (prev_close != m_hover_close) || (prev_preset != m_hover_preset) ||
           (prev_play != m_hover_play) || (prev_reset != m_hover_reset) ||
           (prev_scale != m_hover_scale) || (prev_backend != m_hover_backend) ||
           (prev_snapshot != m_hover_snapshot) || (prev_splitter != m_hover_splitter);
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
        static_cast<void>(m_service.step_frame());
        return true;
    }

    if (m_viewport_mouse_down &&
        layout.shader_panel_viewport_bounds.contains(point_x, point_y))
    {
        const float vx = point_x - layout.shader_panel_viewport_bounds.x;
        const float vy = point_y - layout.shader_panel_viewport_bounds.y;
        m_service.set_mouse(vx, vy, true);
        static_cast<void>(m_service.step_frame());
        return true;
    }

    return false;
}

bool ShaderSandboxPanel::handle_pointer_release() noexcept
{
    const bool was_resizing = m_is_resizing;
    const bool was_mouse_down = m_viewport_mouse_down;
    m_is_resizing = false;
    if (m_viewport_mouse_down)
    {
        m_viewport_mouse_down = false;
        m_service.set_mouse(0.0F, 0.0F, false);
    }
    return was_resizing || was_mouse_down;
}

bool ShaderSandboxPanel::tick_animations() noexcept
{
    if (!m_visible)
    {
        return false;
    }
    return m_service.step_frame();
}

void ShaderSandboxPanel::set_source_code(std::string_view source_code)
{
    m_service.set_shader_source(source_code);
}

void ShaderSandboxPanel::next_preset()
{
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    if (presets.empty())
    {
        return;
    }
    const std::size_t next_idx = (m_service.get_active_preset_index() + 1) % presets.size();
    m_service.load_preset(next_idx);
}

void ShaderSandboxPanel::previous_preset()
{
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    if (presets.empty())
    {
        return;
    }
    const std::size_t prev_idx =
        (m_service.get_active_preset_index() + presets.size() - 1) % presets.size();
    m_service.load_preset(prev_idx);
}

void ShaderSandboxPanel::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    if (!m_visible || layout.shader_panel_bounds.is_empty())
    {
        return;
    }

    // Draw background
    surface.fill_rectangle(
        device_context, layout.shader_panel_bounds, surface.m_palette.sidebar_background);

    render_header(surface, device_context, layout);
    render_viewport(surface, device_context, layout);
    render_controls(surface, device_context, layout);

    // Draw Splitter border on the left edge with blue accent highlight when hovered or resizing
    const bool show_accent = m_hover_splitter || m_is_resizing;
    const UI::Theme::Color& splitter_color = show_accent
        ? surface.m_palette.accent
        : surface.m_palette.border;

    const float scale = surface.m_dpi_scale;
    const float splitter_x = layout.shader_panel_bounds.x;
    surface.draw_line(
        device_context,
        round_to_int(splitter_x),
        round_to_int(layout.shader_panel_bounds.y),
        round_to_int(splitter_x),
        round_to_int(layout.shader_panel_bounds.bottom()),
        splitter_color);

    if (show_accent)
    {
        surface.fill_rectangle(
            device_context,
            UI::Rect{
                splitter_x - 1.5F * scale,
                layout.shader_panel_bounds.y,
                std::max(3.5F * scale, 3.0F),
                layout.shader_panel_bounds.height},
            surface.m_palette.accent);
    }
}

void ShaderSandboxPanel::render_header(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& header = layout.shader_panel_header_bounds;
    const float scale = layout.dpi_scale;

    // Header background & bottom border
    surface.fill_rectangle(device_context, header, surface.m_palette.tab_background);
    surface.draw_line(
        device_context,
        round_to_int(header.x),
        round_to_int(header.bottom() - 1.0F),
        round_to_int(header.right()),
        round_to_int(header.bottom() - 1.0F),
        surface.m_palette.border);

    // Status Dot indicator (Green = Running, Yellow = Paused, Red = Error, Cyan = Compiling)
    UI::Theme::Color status_color = surface.m_palette.success;
    switch (m_service.get_status())
    {
    case Services::Shader::ShaderStatus::Running:
        status_color = surface.m_palette.success;
        break;
    case Services::Shader::ShaderStatus::Paused:
        status_color = surface.m_palette.warning;
        break;
    case Services::Shader::ShaderStatus::Compiling:
        status_color = surface.m_palette.accent;
        break;
    case Services::Shader::ShaderStatus::Error:
        status_color = UI::Theme::Color{220, 50, 50, 255};
        break;
    case Services::Shader::ShaderStatus::Idle:
        status_color = surface.m_palette.text_muted;
        break;
    }

    const float dot_y = header.y + header.height * 0.5F;
    const float dot_r = 3.5F * scale;
    surface.fill_rounded_rectangle(
        device_context,
        UI::Rect{header.x + 12.0F * scale, dot_y - dot_r, dot_r * 2.0F, dot_r * 2.0F},
        status_color,
        dot_r);

    // Panel Title
    if (surface.m_ui_font)
    {
        surface.draw_text(
            device_context,
            *surface.m_ui_font,
            "Shader Sandbox",
            header.x + 26.0F * scale,
            dot_y,
            surface.m_palette.text_primary);
    }

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
        surface.fill_rounded_rectangle(
            device_context, m_header_close_bounds, UI::Theme::Color{255, 255, 255, 25}, 4.0F * scale);
    }
    const UI::Theme::Color close_col = m_hover_close ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted;
    const int close_cx = round_to_int(m_header_close_bounds.x + m_header_close_bounds.width * 0.5F);
    const int close_cy = round_to_int(m_header_close_bounds.y + m_header_close_bounds.height * 0.5F);
    const int close_icon_sz = std::max(round_to_int(10.0F * scale), 8);
    surface.draw_svg_icon(
        device_context, "Assets/icons/close-minimal.svg", close_cx, close_cy, close_icon_sz,
        close_col, surface.m_palette.tab_background);

    // Preset selector dropdown button (dynamic width matching text)
    std::string preset_name = "Custom / Preset";
    const auto presets = Services::Shader::ShaderCompiler::get_starter_presets();
    if (!presets.empty() && m_service.get_active_preset_index() < presets.size())
    {
        preset_name = presets[m_service.get_active_preset_index()].name;
    }
    const int text_w = surface.m_small_font ? surface.get_text_width(device_context, *surface.m_small_font, preset_name) : round_to_int(80.0F * scale);
    const float preset_w = std::clamp(static_cast<float>(text_w) + 26.0F * scale,
                                      90.0F * scale, 160.0F * scale);
    const float preset_x = m_header_close_bounds.x - preset_w - 6.0F * scale;
    m_header_preset_bounds = UI::Rect{
        preset_x,
        header.y + (header.height - 22.0F * scale) * 0.5F,
        preset_w,
        22.0F * scale};

    surface.fill_rounded_rectangle(
        device_context,
        m_header_preset_bounds,
        m_hover_preset ? surface.m_palette.active_line_background : surface.m_palette.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(device_context, m_header_preset_bounds, surface.m_palette.border);

    if (surface.m_small_font)
    {
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            preset_name,
            m_header_preset_bounds.x + 8.0F * scale,
            dot_y,
            m_hover_preset ? surface.m_palette.text_primary : surface.m_palette.text_muted);
    }
    // Chevron indicator on preset button
    surface.draw_svg_icon(
        device_context,
        "Assets/icons/chevron-down.svg",
        round_to_int(m_header_preset_bounds.right() - 10.0F * scale),
        round_to_int(dot_y),
        std::max(round_to_int(8.0F * scale), 6),
        surface.m_palette.text_muted,
        surface.m_palette.sidebar_background,
        false);
}

void ShaderSandboxPanel::render_viewport(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& canvas_rect = layout.shader_panel_viewport_bounds;

    // Resize virtual surface if canvas dimensions changed & step frame
    const int target_w = round_to_int(canvas_rect.width);
    const int target_h = round_to_int(canvas_rect.height);
    if (target_w > 16 && target_h > 16)
    {
        const_cast<ShaderSandboxPanel*>(this)->m_service.resize_surface(target_w, target_h);
        static_cast<void>(const_cast<ShaderSandboxPanel*>(this)->m_service.step_frame());
    }

    // Acquire mapped virtual surface (Android Emulator Surface Mapping Model)
    auto surface_lock = m_service.acquire_mapped_surface();
    if (surface_lock.is_valid())
    {
        const auto pixels = surface_lock.get_pixels();
        const auto& desc = surface_lock.get_descriptor();
        const int img_w = desc.width;
        const int img_h = desc.height;
        const bool is_gpu = (desc.backend == Services::Shader::RenderBackend::Gpu);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = img_w;
        bmi.bmiHeader.biHeight = is_gpu ? img_h : -img_h; // GPU glReadPixels is bottom-up, CPU is top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SaveDC(device_context);
        IntersectClipRect(
            device_context,
            round_to_int(canvas_rect.x),
            round_to_int(canvas_rect.y),
            round_to_int(canvas_rect.right()),
            round_to_int(canvas_rect.bottom()));

        StretchDIBits(
            device_context,
            round_to_int(canvas_rect.x),
            round_to_int(canvas_rect.y),
            round_to_int(canvas_rect.width),
            round_to_int(canvas_rect.height),
            0,
            0,
            img_w,
            img_h,
            pixels.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);

        RestoreDC(device_context, -1);
    }
    else
    {
        surface.fill_rectangle(
            device_context, canvas_rect, UI::Theme::Color{18, 18, 24, 255});
    }

    surface.draw_rectangle(device_context, canvas_rect, surface.m_palette.border);

    // Error overlay banner if compilation failed
    render_diagnostics_overlay(surface, device_context, canvas_rect);
}

void ShaderSandboxPanel::render_controls(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Editor::StudioEditorLayoutResult& layout) const
{
    const UI::Rect& ctrl = layout.shader_panel_controls_bounds;
    const float scale = layout.dpi_scale;

    // Controls toolbar background & top border
    surface.fill_rectangle(device_context, ctrl, surface.m_palette.tab_background);
    surface.draw_line(
        device_context,
        round_to_int(ctrl.x),
        round_to_int(ctrl.y),
        round_to_int(ctrl.right()),
        round_to_int(ctrl.y),
        surface.m_palette.border);

    const float btn_w = 26.0F * scale;
    const float btn_h = 24.0F * scale;
    const float btn_y = ctrl.y + (ctrl.height - btn_h) * 0.5F;

    // Play/Pause button
    m_ctrl_play_bounds = UI::Rect{ctrl.x + 8.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(
        device_context,
        m_ctrl_play_bounds,
        m_hover_play ? surface.m_palette.active_line_background : surface.m_palette.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(device_context, m_ctrl_play_bounds, surface.m_palette.border);

    {
        const float cx = m_ctrl_play_bounds.x + m_ctrl_play_bounds.width * 0.5F;
        const float cy = m_ctrl_play_bounds.y + m_ctrl_play_bounds.height * 0.5F;
        if (m_service.get_engine().is_playing())
        {
            // Elegant two vertical pause bars
            const float bar_w = 2.5F * scale;
            const float bar_h = 9.0F * scale;
            const float bar_gap = 2.0F * scale;
            const UI::Theme::Color bar_color = m_hover_play
                ? surface.m_palette.accent
                : UI::Theme::Color{255, 255, 255, 255};
            surface.fill_rounded_rectangle(device_context,
                UI::Rect{cx - bar_gap - bar_w, cy - bar_h * 0.5F, bar_w, bar_h},
                bar_color, 1.0F * scale);
            surface.fill_rounded_rectangle(device_context,
                UI::Rect{cx + bar_gap, cy - bar_h * 0.5F, bar_w, bar_h},
                bar_color, 1.0F * scale);
        }
        else
        {
            surface.draw_svg_icon(
                device_context,
                "Assets/icons/play.svg",
                round_to_int(cx), round_to_int(cy),
                std::max(round_to_int(11.0F * scale), 9),
                m_hover_play ? surface.m_palette.accent : UI::Theme::Color{255, 255, 255, 255},
                surface.m_palette.sidebar_background,
                false);
        }
    }

    // Reset button
    m_ctrl_reset_bounds = UI::Rect{m_ctrl_play_bounds.right() + 4.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(
        device_context,
        m_ctrl_reset_bounds,
        m_hover_reset ? surface.m_palette.active_line_background : surface.m_palette.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(device_context, m_ctrl_reset_bounds, surface.m_palette.border);
    surface.draw_svg_icon(
        device_context,
        "Assets/icons/refresh.svg",
        round_to_int(m_ctrl_reset_bounds.x + m_ctrl_reset_bounds.width * 0.5F),
        round_to_int(m_ctrl_reset_bounds.y + m_ctrl_reset_bounds.height * 0.5F),
        std::max(round_to_int(11.0F * scale), 9),
        m_hover_reset ? surface.m_palette.accent : surface.m_palette.text_muted,
        surface.m_palette.sidebar_background,
        false);

    // Resolution scale badge button (1x / 0.5x / 0.25x)
    m_ctrl_scale_bounds = UI::Rect{m_ctrl_reset_bounds.right() + 4.0F * scale, btn_y, 44.0F * scale, btn_h};
    surface.fill_rounded_rectangle(
        device_context,
        m_ctrl_scale_bounds,
        m_hover_scale ? surface.m_palette.active_line_background : surface.m_palette.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(device_context, m_ctrl_scale_bounds, surface.m_palette.border);

    std::string scale_str = "1.0x";
    switch (m_service.get_resolution_scale())
    {
    case Services::Shader::ResolutionScale::Full:
        scale_str = "1.0x";
        break;
    case Services::Shader::ResolutionScale::Half:
        scale_str = "0.5x";
        break;
    case Services::Shader::ResolutionScale::Quarter:
        scale_str = "0.25x";
        break;
    }
    if (surface.m_small_font)
    {
        const int text_w = surface.get_text_width(device_context, *surface.m_small_font, scale_str);
        const float text_x = m_ctrl_scale_bounds.x + (m_ctrl_scale_bounds.width - static_cast<float>(text_w)) * 0.5F;
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            scale_str,
            text_x,
            ctrl.y + ctrl.height * 0.5F,
            m_hover_scale ? surface.m_palette.text_primary : surface.m_palette.text_muted);
    }

    // Backend toggle badge button (CPU / GPU)
    m_ctrl_backend_bounds = UI::Rect{m_ctrl_scale_bounds.right() + 4.0F * scale, btn_y, 40.0F * scale, btn_h};
    const bool is_gpu = (m_service.get_render_backend() == Services::Shader::RenderBackend::Gpu);
    const UI::Theme::Color backend_bg = m_hover_backend
        ? surface.m_palette.active_line_background
        : (is_gpu ? UI::Theme::Color{30, 60, 45, 255} : surface.m_palette.sidebar_background);
    const UI::Theme::Color backend_text_col = is_gpu
        ? UI::Theme::Color{80, 220, 140, 255}
        : (m_hover_backend ? surface.m_palette.text_primary : surface.m_palette.text_muted);

    surface.fill_rounded_rectangle(device_context, m_ctrl_backend_bounds, backend_bg, 4.0F * scale);
    surface.draw_rectangle(device_context, m_ctrl_backend_bounds, is_gpu ? UI::Theme::Color{45, 120, 75, 255} : surface.m_palette.border);

    if (surface.m_small_font)
    {
        const std::string_view backend_str = is_gpu ? "GPU" : "CPU";
        const int btext_w = surface.get_text_width(device_context, *surface.m_small_font, backend_str);
        const float btext_x = m_ctrl_backend_bounds.x + (m_ctrl_backend_bounds.width - static_cast<float>(btext_w)) * 0.5F;
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            backend_str,
            btext_x,
            ctrl.y + ctrl.height * 0.5F,
            backend_text_col);
    }

    // Time elapsed & frametime display
    std::ostringstream time_ss;
    time_ss << std::fixed << std::setprecision(1) << m_service.get_time() << "s ("
            << std::fixed << std::setprecision(1) << m_service.get_frame_time_ms() << "ms)";
    if (surface.m_small_font)
    {
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            time_ss.str(),
            m_ctrl_backend_bounds.right() + 10.0F * scale,
            ctrl.y + ctrl.height * 0.5F,
            surface.m_palette.text_muted);
    }

    // Snapshot button (Capture art)
    m_ctrl_snapshot_bounds = UI::Rect{ctrl.right() - btn_w - 8.0F * scale, btn_y, btn_w, btn_h};
    surface.fill_rounded_rectangle(
        device_context,
        m_ctrl_snapshot_bounds,
        m_hover_snapshot ? surface.m_palette.active_line_background : surface.m_palette.sidebar_background,
        4.0F * scale);
    surface.draw_rectangle(device_context, m_ctrl_snapshot_bounds, surface.m_palette.border);
    surface.draw_svg_icon(
        device_context,
        "Assets/icons/build.svg",
        round_to_int(m_ctrl_snapshot_bounds.x + m_ctrl_snapshot_bounds.width * 0.5F),
        round_to_int(m_ctrl_snapshot_bounds.y + m_ctrl_snapshot_bounds.height * 0.5F),
        std::max(round_to_int(11.0F * scale), 9),
        m_hover_snapshot ? surface.m_palette.accent : surface.m_palette.text_muted,
        surface.m_palette.sidebar_background,
        false);
}

void ShaderSandboxPanel::render_diagnostics_overlay(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    const UI::Rect& viewport_rect) const
{
    const auto& diagnostics = m_service.get_diagnostics();
    if (diagnostics.empty() ||
        m_service.get_status() != Services::Shader::ShaderStatus::Error)
    {
        return;
    }

    const float scale = surface.m_dpi_scale;
    const float banner_height = 24.0F * scale;
    const UI::Rect banner_rect{
        viewport_rect.x,
        viewport_rect.bottom() - banner_height,
        viewport_rect.width,
        banner_height};

    surface.fill_rectangle(
        device_context, banner_rect, UI::Theme::Color{180, 40, 40, 220});

    std::string err_msg = "Error: ";
    if (!diagnostics.empty())
    {
        err_msg += diagnostics.front().message;
        if (diagnostics.front().line > 0)
        {
            err_msg += " (line " + std::to_string(diagnostics.front().line) + ")";
        }
    }

    if (surface.m_small_font)
    {
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            err_msg,
            banner_rect.x + 8.0F * scale,
            banner_rect.y + banner_height * 0.5F,
            UI::Theme::Color{255, 255, 255, 255});
    }
}

} // namespace Zenvra::Platform::Win32::Components
