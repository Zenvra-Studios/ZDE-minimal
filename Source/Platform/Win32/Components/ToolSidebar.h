#pragma once

#include "Platform/Win32/Components/ExplorerHeader.h"
#include "UI/Components/Button.h"
#include "UI/Components/ScrollBar.h"
#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Editor/WorkspaceSearchModel.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <optional>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

enum class SidebarActionKind {
    None,
    OpenFile,
    NewFile,
    NewFolder,
    Refresh,
    CollapseAll
};

struct SidebarPressResult {
    bool handled = false;
    SidebarActionKind action = SidebarActionKind::None;
    std::optional<std::filesystem::path> path = std::nullopt;
    std::size_t line = 0;
    std::size_t column = 0;
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

    [[nodiscard]] bool handle_char(char32_t codepoint);
    [[nodiscard]] bool handle_key(int vkey, bool ctrl, bool shift, bool alt);
    [[nodiscard]] bool is_search_focused() const noexcept;

    [[nodiscard]] UI::Editor::ActivityPanelModel& get_model() noexcept { return m_model; }
    [[nodiscard]] const UI::Editor::ActivityPanelModel& get_model() const noexcept { return m_model; }
    [[nodiscard]] UI::Editor::WorkspaceSearchModel& get_search_model() noexcept { return m_search_model; }
    [[nodiscard]] const UI::Editor::WorkspaceSearchModel& get_search_model() const noexcept { return m_search_model; }

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_active(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool is_hovered(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_interactive_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    [[nodiscard]] bool is_text_input_point(
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
    [[nodiscard]] bool is_dragging_scrollbar() const noexcept { return m_project_scrollbar.is_dragging() || m_search_scrollbar.is_dragging(); }
    [[nodiscard]] bool tick_animations() noexcept;

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr float default_width = 260.0F;
    // Keep the Explorer header aligned with the editor tab strip. Both
    // borders must use the same vertical measurement when a buffer opens.
    static constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
    static constexpr float row_height = 22.0F;

    [[nodiscard]] std::size_t viewport_row_count(const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::size_t search_viewport_row_count(const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] float search_tree_top_y(const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::optional<std::size_t> row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;
    [[nodiscard]] std::optional<std::size_t> search_row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;
    [[nodiscard]] UI::Rect scrollbar_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] UI::Rect search_scrollbar_bounds(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::vector<std::size_t> get_sticky_items() const;

    void render_search_panel(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;
    [[nodiscard]] SidebarPressResult handle_search_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y);
    [[nodiscard]] bool handle_search_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;

    UI::Editor::ActivityPanelModel m_model;
    UI::Editor::WorkspaceSearchModel m_search_model;
    ExplorerHeader m_explorer_header;
    mutable UI::Components::Button m_empty_state_open_btn;
    mutable UI::Components::Button m_empty_state_clone_btn;
    std::optional<std::size_t> m_hovered_row;
    std::optional<std::size_t> m_hovered_search_row;
    std::optional<std::size_t> m_hovered_sticky_index;
    std::optional<UI::Editor::SidebarIcon> m_hovered_icon;
    mutable UI::Components::ScrollBar m_project_scrollbar;
    mutable UI::Components::ScrollBar m_search_scrollbar;
    bool m_hovered_scrollbar = false;
    bool m_hovered_search_scrollbar = false;

    // Search UI Hover States
    bool m_hover_search_chevron = false;
    bool m_hover_search_match_case = false;
    bool m_hover_search_match_word = false;
    bool m_hover_search_use_regex = false;
    bool m_hover_search_preserve_case = false;
    bool m_hover_search_replace_all = false;
    bool m_hover_search_refresh = false;
    bool m_hover_search_clear = false;
    bool m_hover_search_collapse_all = false;

    float m_width = default_width;
    bool m_resizing = false;
    bool m_resize_hovered = false;
    float m_drag_start_x = 0.0F;
    float m_drag_start_width = 0.0F;

    // macOS-style Drag & Drop item moving
    std::optional<std::size_t> m_drag_source_row;
    std::optional<std::size_t> m_drag_target_row;
    float m_drag_press_x = 0.0F;
    float m_drag_press_y = 0.0F;
    float m_drag_current_x = 0.0F;
    float m_drag_current_y = 0.0F;
    bool m_is_dragging_item = false;
    bool m_is_selecting_search_text = false;
    std::uint64_t m_last_search_click_time = 0;
    std::chrono::steady_clock::time_point m_last_refresh_time{};
};

} // namespace Zenvra::Platform::Win32::Components
