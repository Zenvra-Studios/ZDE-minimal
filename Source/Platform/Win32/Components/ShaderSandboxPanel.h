#pragma once

#include "Services/Shader/ShaderService.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class ShaderSandboxPanel
{
public:
    ShaderSandboxPanel();
    ~ShaderSandboxPanel();

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool toggle() noexcept;
    void set_visible(bool visible) noexcept;
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }
    [[nodiscard]] float get_width() const noexcept { return m_width; }

    [[nodiscard]] bool is_resize_handle_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;

    [[nodiscard]] bool is_resizing() const noexcept { return m_is_resizing; }
    void set_resize_hovered(bool hovered) noexcept { m_hover_splitter = hovered; }
    void begin_resize(float point_x) noexcept {
        m_is_resizing = true;
        m_drag_start_x = point_x;
        m_drag_start_width = m_width;
        m_hover_splitter = true;
    }

    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;

    [[nodiscard]] bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y);

    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;

    [[nodiscard]] bool handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;

    [[nodiscard]] bool handle_pointer_release() noexcept;

    [[nodiscard]] bool tick_animations() noexcept;

    void set_source_code(std::string_view source_code);
    void next_preset();
    void previous_preset();

    [[nodiscard]] Services::Shader::ShaderService& get_service() noexcept { return m_service; }
    [[nodiscard]] const Services::Shader::ShaderService& get_service() const noexcept { return m_service; }

    [[nodiscard]] Services::Shader::ShaderRuntimeEngine& get_engine() noexcept { return m_service.get_engine(); }
    [[nodiscard]] const Services::Shader::ShaderRuntimeEngine& get_engine() const noexcept { return m_service.get_engine(); }

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    void render_header(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

    void render_viewport(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

    void render_controls(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

    void render_diagnostics_overlay(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Rect& viewport_rect) const;

    Services::Shader::ShaderService m_service;
    bool m_visible = false;
    float m_width = 380.0F;
    bool m_is_resizing = false;
    float m_drag_start_x = 0.0F;
    float m_drag_start_width = 380.0F;

    // Interactive button hover states
    bool m_hover_close = false;
    bool m_hover_preset = false;
    bool m_hover_play = false;
    bool m_hover_reset = false;
    bool m_hover_scale = false;
    bool m_hover_backend = false;
    bool m_hover_snapshot = false;
    bool m_hover_splitter = false;
    bool m_viewport_mouse_down = false;

    mutable UI::Rect m_header_close_bounds{};
    mutable UI::Rect m_header_preset_bounds{};
    mutable UI::Rect m_ctrl_play_bounds{};
    mutable UI::Rect m_ctrl_reset_bounds{};
    mutable UI::Rect m_ctrl_scale_bounds{};
    mutable UI::Rect m_ctrl_backend_bounds{};
    mutable UI::Rect m_ctrl_snapshot_bounds{};
};

} // namespace Zenvra::Platform::Win32::Components
