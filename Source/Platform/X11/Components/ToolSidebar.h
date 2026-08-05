#pragma once

#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Components/Button.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <filesystem>
#include <optional>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

class ToolSidebar
{
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
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
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr float default_width = 260.0F;
    // Keep Explorer aligned with the editor tab strip on both platforms.
    static constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
    static constexpr float row_height = 22.0F;

    [[nodiscard]] std::size_t viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::optional<std::size_t> row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;
    [[nodiscard]] UI::Rect scrollbar_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;

    UI::Editor::ActivityPanelModel m_model;
    mutable UI::Components::Button m_empty_state_open_btn;
    mutable UI::Components::Button m_empty_state_clone_btn;
    std::optional<std::size_t> m_hovered_row;
    std::optional<UI::Editor::SidebarIcon> m_hovered_icon;
    bool m_hovered_scrollbar = false;
};

} // namespace Zenvra::Platform::X11::Components
