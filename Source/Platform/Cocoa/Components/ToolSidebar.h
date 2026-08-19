#pragma once

#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/WorkspaceSearchModel.h"
#include "UI/Editor/WorkspaceSourceControlModel.h"
#include "UI/Components/Button.h"
#include "UI/Editor/StudioEditorModel.h"
#include "UI/Editor/TextDocumentModel.h"
#include "UI/Editor/EditorSessionModel.h"
#include "Platform/Cocoa/Components/ExplorerHeader.h"

#include <CoreGraphics/CoreGraphics.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace Zenvra::Platform::Cocoa::Components
{

class StudioWorkspaceRenderer;

class ToolSidebar
{
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] bool activate(UI::Editor::SidebarIcon icon) noexcept;
    [[nodiscard]] bool handle_pointer_press(
        StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y,
        std::optional<std::filesystem::path>& file_to_open,
        std::optional<std::size_t>& target_line,
        std::optional<std::size_t>& target_col);
    [[nodiscard]] bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_scroll(
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::ptrdiff_t line_delta) noexcept;

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_active(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool is_hovered(UI::Editor::SidebarIcon icon) const noexcept;
    [[nodiscard]] bool contains(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_resize_handle_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) const noexcept;
    [[nodiscard]] bool is_resizing() const noexcept;
    [[nodiscard]] float get_width() const noexcept;

    [[nodiscard]] bool handle_pointer_drag(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) noexcept;
    [[nodiscard]] bool handle_pointer_release() noexcept;
    [[nodiscard]] bool tick_animations() noexcept;

    [[nodiscard]] bool is_search_focused() const noexcept;
    [[nodiscard]] bool handle_search_text(std::string_view text);
    [[nodiscard]] bool handle_search_command(UI::Editor::EditorInputCommand cmd, bool extend);
    [[nodiscard]] bool handle_search_action(UI::Editor::EditorAction action);

    [[nodiscard]] bool is_source_control_focused() const noexcept;
    [[nodiscard]] bool handle_source_control_text(std::string_view text);
    [[nodiscard]] bool handle_source_control_command(UI::Editor::EditorInputCommand cmd, bool extend);
    [[nodiscard]] bool handle_source_control_action(UI::Editor::EditorAction action);

    [[nodiscard]] UI::Editor::ActivityPanelModel& get_model() noexcept { return m_model; }
    [[nodiscard]] const UI::Editor::ActivityPanelModel& get_model() const noexcept { return m_model; }
    [[nodiscard]] UI::Editor::WorkspaceSearchModel& get_search_model() noexcept { return m_search_model; }
    [[nodiscard]] const UI::Editor::WorkspaceSearchModel& get_search_model() const noexcept { return m_search_model; }
    [[nodiscard]] UI::Editor::WorkspaceSourceControlModel& get_source_control_model() noexcept { return m_source_control_model; }
    [[nodiscard]] const UI::Editor::WorkspaceSourceControlModel& get_source_control_model() const noexcept { return m_source_control_model; }
    [[nodiscard]] std::optional<std::size_t> row_at_point(
        const UI::Editor::StudioEditorLayoutResult& layout, float py) const noexcept
    {
        return row_from_point(layout, py);
    }

    void render(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    static constexpr float default_width = 260.0F;
    static constexpr float header_height = UI::Editor::StudioEditorMetrics::tab_height;
    static constexpr float row_height = 22.0F;

    [[nodiscard]] std::size_t viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] std::size_t search_viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] float search_tree_top_y(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
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
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;
    [[nodiscard]] bool handle_search_press(
        StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y,
        std::optional<std::filesystem::path>& file_to_open,
        std::optional<std::size_t>& target_line,
        std::optional<std::size_t>& target_col);
    [[nodiscard]] bool handle_search_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) noexcept;

    // Source Control Panel
    void render_source_control_panel(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;
    [[nodiscard]] bool handle_source_control_press(
        StudioWorkspaceRenderer& surface,
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y,
        std::optional<std::filesystem::path>& file_to_open);
    [[nodiscard]] bool handle_source_control_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x, float point_y) noexcept;
    [[nodiscard]] std::optional<std::size_t> source_control_row_from_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_y) const noexcept;
    [[nodiscard]] std::size_t source_control_viewport_row_count(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;
    [[nodiscard]] float source_control_tree_top_y(
        const UI::Editor::StudioEditorLayoutResult& layout) const noexcept;

    UI::Editor::ActivityPanelModel m_model;
    UI::Editor::WorkspaceSearchModel m_search_model;
    UI::Editor::WorkspaceSourceControlModel m_source_control_model;
    ExplorerHeader m_explorer_header;
    mutable UI::Components::Button m_empty_state_open_btn;
    mutable UI::Components::Button m_empty_state_clone_btn;
    std::optional<std::size_t> m_hovered_row;
    std::optional<std::size_t> m_hovered_search_row;
    std::optional<std::size_t> m_hovered_sc_row;
    std::optional<std::size_t> m_hovered_sticky_index;
    std::optional<UI::Editor::SidebarIcon> m_hovered_icon;
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

    // Source Control UI Hover States
    bool m_hover_sc_refresh = false;
    bool m_hover_sc_more = false;
    bool m_hover_sc_commit_btn = false;
    bool m_hover_sc_repo_refresh = false;
    bool m_hover_sc_repo_commit = false;
    bool m_hover_sc_repo_discard = false;
    bool m_hover_sc_repo_more = false;
    bool m_hover_sc_stage_all = false;
    bool m_hover_sc_unstage_all = false;
    bool m_hover_sc_discard_all = false;
    bool m_hover_sc_graph_refresh = false;
    std::optional<std::size_t> m_hover_sc_action_row;
    bool m_hover_sc_row_action_open_diff = false;
    bool m_hover_sc_row_action_stage = false;
    bool m_hover_sc_row_action_discard = false;

    float m_width = default_width;
    bool m_resizing = false;
    bool m_resize_hovered = false;
    float m_drag_start_x = 0.0F;
    float m_drag_start_width = 0.0F;

    // Drag & Drop Item Moving
    std::optional<std::size_t> m_drag_source_row;
    std::optional<std::size_t> m_drag_target_row;
    float m_drag_press_x = 0.0F;
    float m_drag_press_y = 0.0F;
    float m_drag_current_x = 0.0F;
    float m_drag_current_y = 0.0F;
    bool m_is_dragging_item = false;
    bool m_is_selecting_search_text = false;
    bool m_is_selecting_sc_text = false;
    std::uint64_t m_last_search_click_time = 0;
};

} // namespace Zenvra::Platform::Cocoa::Components
