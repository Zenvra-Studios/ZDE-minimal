#pragma once

#include "UI/Editor/EditorMinimapModel.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Editor/TextDocumentModel.h"

#include <windows.h>

#include <cstddef>
#include <optional>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class EditorMinimap
{
public:
    [[nodiscard]] bool is_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        std::size_t total_lines,
        std::size_t visible_lines,
        std::size_t first_visible_line) noexcept;
    [[nodiscard]] std::optional<std::size_t> handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y,
        std::size_t total_lines,
        std::size_t visible_lines,
        std::size_t first_visible_line) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout,
        const UI::Editor::TextDocumentModel& document,
        std::size_t first_visible_line,
        std::size_t visible_lines) const;

private:
    mutable UI::Editor::EditorMinimapModel m_model;
    bool m_pointer_dragging = false;
};

} // namespace Zenvra::Platform::Win32::Components
