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
    [[nodiscard]] ActivityPanelAction activate_project_row(std::size_t visible_row);
    [[nodiscard]] bool scroll(std::ptrdiff_t line_delta, std::size_t viewport_rows) noexcept;

    [[nodiscard]] bool is_visible() const noexcept;
    [[nodiscard]] bool is_active(SidebarIcon icon) const noexcept;
    [[nodiscard]] SidebarIcon get_active_icon() const noexcept;
    [[nodiscard]] std::string_view get_title() const noexcept;
    [[nodiscard]] std::string_view get_content_heading() const noexcept;
    [[nodiscard]] std::string_view get_content_detail() const noexcept;
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept;
    [[nodiscard]] std::span<const ProjectTreeItem> get_project_items() const noexcept;
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept;

private:
    static constexpr std::size_t maximum_tree_items = 2048;

    [[nodiscard]] bool is_expanded(const std::filesystem::path& path) const;
    void toggle_expanded(const std::filesystem::path& path);
    void append_directory(const std::filesystem::path& directory, std::size_t depth);
    void rebuild_tree();

    std::filesystem::path m_workspace_root;
    std::vector<ProjectTreeItem> m_project_items;
    std::vector<std::filesystem::path> m_expanded_paths;
    std::size_t m_scroll_offset = 0;
    SidebarIcon m_active_icon = SidebarIcon::Project;
    bool m_visible = true;
};

} // namespace Zenvra::UI::Editor
