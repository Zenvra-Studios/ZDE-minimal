#pragma once

#include "Platform/X11/Components/ExplorerHeader.h"
#include "UI/Components/Button.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

enum class SidebarActionKind {
    NoneAction,
    OpenFile,
    NewFile,
    NewFolder,
    Refresh,
    CollapseAll
};

struct SidebarPressResult {
    bool handled = false;
    SidebarActionKind action = SidebarActionKind::NoneAction;
    std::optional<std::filesystem::path> path;
};

class ToolSidebar
{
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    void clear_workspace() noexcept;
    [[nodiscard]] bool activate(UI::Editor::SidebarIcon icon) noexcept;
    [[nodiscard]] SidebarPressResult handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y);
    [[nodiscard]] std::optional<std::filesystem::path> handle_right_click(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y);
    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool handle_scroll(
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::ptrdiff_t line_delta) noexcept;

    [[nodiscard]] UI::Editor::ActivityPanelModel& get_model() noexcept { return m_model; }
    [[nodiscard]] const UI::Editor::ActivityPanelModel& get_model() const noexcept { return m_model; }

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_active(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool is_hovered(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_resize_handle_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_resizing() const noexcept;
    [[nodiscard]] float get_width() const noexcept;

    [[nodiscard]] bool handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool is_dragging_item() const noexcept { return m_is_dragging_item; }
    [[nodiscard]] bool tick_animations() noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr float default_width = 260.0F;
    static constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
    static constexpr float row_height = 22.0F;

    [[nodiscard]] std::size_t viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::optional<std::size_t> row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;
    [[nodiscard]] UI::Rect scrollbar_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::vector<std::size_t> get_sticky_items() const;

    UI::Editor::ActivityPanelModel m_model;
    ExplorerHeader m_explorer_header;
    mutable UI::Components::Button m_empty_state_open_btn;
    mutable UI::Components::Button m_empty_state_clone_btn;
    std::optional<std::size_t> m_hovered_row;
    std::optional<std::size_t> m_hovered_sticky_index;
    std::optional<UI::Editor::SidebarIcon> m_hovered_icon;
    bool m_hovered_scrollbar = false;

    float m_width = default_width;
    bool m_resizing = false;
    bool m_resize_hovered = false;
    float m_drag_start_x = 0.0F;
    float m_drag_start_width = 0.0F;

    // Drag & Drop item moving
    std::optional<std::size_t> m_drag_source_row;
    std::optional<std::size_t> m_drag_target_row;
    float m_drag_press_x = 0.0F;
    float m_drag_press_y = 0.0F;
    float m_drag_current_x = 0.0F;
    float m_drag_current_y = 0.0F;
    bool m_is_dragging_item = false;
    std::chrono::steady_clock::time_point m_last_refresh_time{};
};

} // namespace Zenvra::Platform::X11::Components
