#pragma once

#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <filesystem>
#include <optional>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class ToolSidebar
{
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool activate(UI::Editor::SidebarIcon icon) noexcept;
    [[nodiscard]] bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        std::optional<std::filesystem::path>& file_to_open);
    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool handle_scroll(
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::ptrdiff_t line_delta) noexcept;

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_active(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool is_hovered(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] float get_width() const noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr float default_width = 260.0F;
    static constexpr float header_height = 36.0F;
    static constexpr float row_height = 22.0F;

    [[nodiscard]] std::size_t viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::optional<std::size_t> row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;

    UI::Editor::ActivityPanelModel m_model;
    std::optional<std::size_t> m_hovered_row;
    std::optional<UI::Editor::SidebarIcon> m_hovered_icon;
};

} // namespace Zenvra::Platform::Win32::Components
