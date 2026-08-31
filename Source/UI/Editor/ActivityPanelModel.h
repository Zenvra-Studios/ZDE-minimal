#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Editor
{

struct ProjectTreeItem
{
    std::filesystem::path path;
    std::string label;
    std::size_t depth = 0;
    bool directory = false;
    bool expanded = false;
};

struct ActivityPanelAction
{
    bool handled = false;
    bool layout_changed = false;
    std::optional<std::filesystem::path> file_to_open;
};

class ActivityPanelModel
{
public:
    [[nodiscard]] bool initialize(const std::filesystem::path& workspace_root = {});
    [[nodiscard]] bool activate(SidebarIcon icon) noexcept;
    [[nodiscard]] bool refresh();
    [[nodiscard]] ActivityPanelAction activate_project_row(std::size_t visible_row, bool shift = false, bool ctrl = false);
    [[nodiscard]] ActivityPanelAction activate_project_item(std::size_t item_index, bool shift = false, bool ctrl = false);
    [[nodiscard]] bool scroll(std::ptrdiff_t line_delta, std::size_t viewport_rows) noexcept;
    void set_scroll_offset(std::size_t offset) noexcept;

    [[nodiscard]] bool is_visible() const noexcept;
    void collapse_all() noexcept;
    void clear_workspace() noexcept;

    void set_visible(bool visible) noexcept;
    [[nodiscard]] bool is_active(SidebarIcon icon) const noexcept;
    [[nodiscard]] SidebarIcon get_active_icon() const noexcept;
    [[nodiscard]] std::string_view get_title() const noexcept;
    [[nodiscard]] std::string_view get_content_heading() const noexcept;
    [[nodiscard]] std::string_view get_content_detail() const noexcept;
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept;
    [[nodiscard]] std::span<const ProjectTreeItem> get_project_items() const noexcept;
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept;

    // Multi-Selection and item tracking
    void set_selected_path(std::optional<std::filesystem::path> path) noexcept;
    void select_single(const std::filesystem::path& path) noexcept;
    void select_toggle(const std::filesystem::path& path) noexcept;
    void select_range(const std::filesystem::path& path) noexcept;
    [[nodiscard]] const std::optional<std::filesystem::path>& get_selected_path() const noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& get_selected_paths() const noexcept { return m_selected_paths; }
    [[nodiscard]] bool is_selected(const std::filesystem::path& path) const noexcept;
    [[nodiscard]] std::filesystem::path get_target_directory_for_creation() const;
    [[nodiscard]] std::optional<std::size_t> get_selected_index() const noexcept;

    // Arrow Key Selection & Navigation
    [[nodiscard]] bool select_next() noexcept;
    [[nodiscard]] bool select_previous() noexcept;
    [[nodiscard]] bool select_parent_or_collapse();
    [[nodiscard]] bool select_first_child_or_expand();
    [[nodiscard]] ActivityPanelAction activate_selected();

    // File / Directory operations
    [[nodiscard]] bool create_file(std::string_view relative_name, std::filesystem::path& out_path);
    [[nodiscard]] bool create_directory(std::string_view relative_name, std::filesystem::path& out_path);
    [[nodiscard]] bool rename_item(const std::filesystem::path& old_path, std::string_view new_name, std::filesystem::path& out_path);
    [[nodiscard]] bool move_item(const std::filesystem::path& source_path, const std::filesystem::path& destination_directory, std::filesystem::path& out_path);
    [[nodiscard]] bool copy_item(const std::filesystem::path& source_path, const std::filesystem::path& destination_directory, std::filesystem::path& out_path);
    [[nodiscard]] bool delete_item(const std::filesystem::path& target_path);
    [[nodiscard]] bool delete_selected_items();

    // Explorer Clipboard Operations
    void copy_to_clipboard(const std::filesystem::path& path) noexcept;
    void cut_to_clipboard(const std::filesystem::path& path) noexcept;
    void copy_selected_to_clipboard() noexcept;
    void cut_selected_to_clipboard() noexcept;
    [[nodiscard]] bool paste_from_clipboard(std::filesystem::path& out_path);
    [[nodiscard]] bool is_cut_path(const std::filesystem::path& path) const noexcept;
    [[nodiscard]] bool can_paste() const noexcept;
    void clear_clipboard() noexcept;
    [[nodiscard]] const std::vector<std::filesystem::path>& get_clipboard_paths() const noexcept { return m_clipboard_paths; }
    [[nodiscard]] bool is_clipboard_cut() const noexcept { return m_clipboard_is_cut; }

private:
    static constexpr std::size_t maximum_tree_items = 2048;

    [[nodiscard]] bool is_expanded(const std::filesystem::path& path) const;
    void toggle_expanded(const std::filesystem::path& path);
    void append_directory(const std::filesystem::path& directory, std::size_t depth);
    void rebuild_tree();

    std::filesystem::path m_workspace_root;
    std::vector<ProjectTreeItem> m_project_items;
    std::vector<std::filesystem::path> m_expanded_paths;
    std::vector<std::filesystem::path> m_selected_paths;
    std::optional<std::filesystem::path> m_selected_path;
    std::optional<std::filesystem::path> m_selection_anchor;
    std::vector<std::filesystem::path> m_clipboard_paths;
    bool m_clipboard_is_cut = false;
    std::size_t m_scroll_offset = 0;
    SidebarIcon m_active_icon = SidebarIcon::Project;
    bool m_visible = true;
};

} // namespace Zenvra::UI::Editor
