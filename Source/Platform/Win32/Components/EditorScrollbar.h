#pragma once

#include "UI/Editor/EditorScrollModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <cstddef>

/**
 * 
 * 
 **/
namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class EditorScrollbar
{
public:
    void reset() noexcept;
    void synchronize(std::size_t total_lines, std::size_t visible_lines) noexcept;
    [[nodiscard]] bool scroll_lines(std::ptrdiff_t line_delta) noexcept;
    [[nodiscard]] bool scroll_to(std::size_t first_visible_line) noexcept;
    [[nodiscard]] bool reveal_line(std::size_t line_index) noexcept;
    [[nodiscard]] bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool is_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool set_hovered(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool is_dragging() const noexcept { return m_model.is_dragging(); }
    [[nodiscard]] std::size_t get_first_visible_line() const noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    [[nodiscard]] static UI::Rect get_track_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) noexcept;

    UI::Editor::EditorScrollModel m_model;
    bool m_hovered = false;
};

} // namespace Zenvra::Platform::Win32::Components
