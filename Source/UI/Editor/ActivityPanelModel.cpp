#include "UI/Editor/ActivityPanelModel.h"

#include "UI/Editor/EditorFileSystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Zenvra::UI::Editor
{

namespace
{

bool is_panel_icon(SidebarIcon icon) noexcept
{
    return icon != SidebarIcon::Terminal;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}


} // namespace

bool ActivityPanelModel::initialize(const std::filesystem::path& workspace_root)
{
    std::error_code error;
    std::filesystem::path resolved_root = workspace_root;
    if (resolved_root.empty())
    {
        if (!m_workspace_root.empty() && std::filesystem::is_directory(m_workspace_root, error))
        {
            resolved_root = m_workspace_root;
        }
        else
        {
            const std::filesystem::path current = std::filesystem::current_path(error);
            if (!error)
            {
                const auto proj_root = EditorFileSystem::find_project_root(current);
                if (proj_root && std::filesystem::is_directory(*proj_root, error))
                {
                    resolved_root = *proj_root;
                }
            }
        }
    }

    if (resolved_root.empty())
    {
        m_workspace_root.clear();
        m_project_items.clear();
        m_expanded_paths.clear();
        m_active_icon = SidebarIcon::Project;
        m_visible = true;
        m_scroll_offset = 0;
        return true;
    }

    resolved_root = std::filesystem::weakly_canonical(resolved_root, error);
    if (error || !std::filesystem::is_directory(resolved_root, error))
    {
        return false;
    }

    m_workspace_root = std::move(resolved_root);
    m_expanded_paths.clear();
    m_expanded_paths.push_back(m_workspace_root);

    m_active_icon = SidebarIcon::Project;
    m_visible = true;
    m_scroll_offset = 0;
    rebuild_tree();
    return true;
}

bool ActivityPanelModel::activate(SidebarIcon icon) noexcept
{
    if (!is_panel_icon(icon))
    {
        return false;
    }
    if (m_visible && m_active_icon == icon)
    {
        m_visible = false;
        return true;
    }
    m_active_icon = icon;
    m_visible = true;
    m_scroll_offset = 0;
    return true;
}

bool ActivityPanelModel::refresh()
{
    if (m_workspace_root.empty())
    {
        return false;
    }
    const auto previous = m_project_items;
    rebuild_tree();
    if (previous.size() != m_project_items.size())
    {
        return true;
    }
    for (std::size_t i = 0; i < previous.size(); ++i)
    {
        if (previous[i].path != m_project_items[i].path ||
            previous[i].label != m_project_items[i].label ||
            previous[i].depth != m_project_items[i].depth ||
            previous[i].directory != m_project_items[i].directory ||
            previous[i].expanded != m_project_items[i].expanded)
        {
            return true;
        }
    }
    return false;
}

void ActivityPanelModel::collapse_all() noexcept
{
    m_expanded_paths.clear();
    rebuild_tree();
}

ActivityPanelAction ActivityPanelModel::activate_project_row(std::size_t visible_row)
{
    if (!m_visible || m_active_icon != SidebarIcon::Project)
    {
        return {};
    }
    const std::size_t item_index = m_scroll_offset + visible_row;
    return activate_project_item(item_index);
}

ActivityPanelAction ActivityPanelModel::activate_project_item(std::size_t item_index)
{
    if (!m_visible || m_active_icon != SidebarIcon::Project)
    {
        return {};
    }
    if (item_index >= m_project_items.size())
    {
        return {};
    }

    const ProjectTreeItem& item = m_project_items[item_index];
    m_selected_path = item.path;
    if (!item.directory)
    {
        return ActivityPanelAction{.handled = true, .file_to_open = item.path};
    }
    toggle_expanded(item.path);
    rebuild_tree();
    return ActivityPanelAction{
        .handled = true,
        .layout_changed = true,
        .file_to_open = std::nullopt,
    };
}

bool ActivityPanelModel::scroll(
    std::ptrdiff_t line_delta,
    std::size_t viewport_rows) noexcept
{
    if (!m_visible || m_active_icon != SidebarIcon::Project || line_delta == 0)
    {
        return false;
    }
    const std::size_t maximum_offset = m_project_items.size() > viewport_rows
        ? m_project_items.size() - viewport_rows
        : 0;
    const std::size_t previous = m_scroll_offset;
    if (line_delta > 0)
    {
        m_scroll_offset = std::min(
            maximum_offset,
            m_scroll_offset + static_cast<std::size_t>(line_delta));
    }
    else
    {
        const std::size_t amount = static_cast<std::size_t>(-line_delta);
        const std::size_t unclamped = amount > m_scroll_offset ? 0 : m_scroll_offset - amount;
        m_scroll_offset = std::min(unclamped, maximum_offset);
    }
    return m_scroll_offset != previous;
}

void ActivityPanelModel::set_scroll_offset(std::size_t offset) noexcept
{
    m_scroll_offset = std::min(offset, m_project_items.empty() ? 0U : m_project_items.size() - 1);
}

bool ActivityPanelModel::is_visible() const noexcept { return m_visible; }

void ActivityPanelModel::clear_workspace() noexcept
{
    m_workspace_root.clear();
    m_project_items.clear();
    m_expanded_paths.clear();
    m_selected_path.reset();
    m_scroll_offset = 0;
}

void ActivityPanelModel::set_visible(bool visible) noexcept
{
    m_visible = visible;
}

bool ActivityPanelModel::is_active(SidebarIcon icon) const noexcept
{
    return m_visible && m_active_icon == icon;
}

std::string_view ActivityPanelModel::get_title() const noexcept
{
    switch (m_active_icon)
    {
    case SidebarIcon::Project: return "Explorer";
    case SidebarIcon::Search: return "Search";
    case SidebarIcon::VersionControl: return "Source Control";
    case SidebarIcon::Run: return "Run & Debug";
    case SidebarIcon::Terminal: return "Terminal";
    case SidebarIcon::Services: return "Services";
    case SidebarIcon::Problems: return "Problems";
    case SidebarIcon::Shader: return "Shader Sandbox";
    case SidebarIcon::More: return "More";
    default: return "Activity";
    }
}

std::string_view ActivityPanelModel::get_content_heading() const noexcept
{
    switch (m_active_icon)
    {
    case SidebarIcon::VersionControl: return "No pending changes";
    case SidebarIcon::Search: return "Search across the workspace";
    case SidebarIcon::Services: return "No services configured";
    case SidebarIcon::More: return "Available tool windows";
    case SidebarIcon::Run: return "Run and debug";
    case SidebarIcon::Problems: return "No problems detected";
    case SidebarIcon::Project: return "Workspace files";
    case SidebarIcon::Terminal: return "Local terminal";
    case SidebarIcon::Shader: return "Shader sandbox";
    default: return {};
    }
}

std::string_view ActivityPanelModel::get_content_detail() const noexcept
{
    switch (m_active_icon)
    {
    case SidebarIcon::VersionControl: return "Workspace changes will appear here.";
    case SidebarIcon::Search: return "Search results will appear in this panel.";
    case SidebarIcon::Services: return "Registered local services will appear here.";
    case SidebarIcon::More: return "Explorer, Search, Git, Run, Terminal, Problems";
    case SidebarIcon::Run: return "Create a run configuration to start debugging.";
    case SidebarIcon::Problems: return "Diagnostics from opened buffers appear here.";
    case SidebarIcon::Project: return "Browse and open files from the project tree.";
    case SidebarIcon::Terminal: return "Terminal sessions use the bottom panel.";
    case SidebarIcon::Shader: return "Realtime GLSL shader preview and editor.";
    default: return {};
    }
}

const std::filesystem::path& ActivityPanelModel::get_workspace_root() const noexcept
{
    return m_workspace_root;
}

std::span<const ProjectTreeItem> ActivityPanelModel::get_project_items() const noexcept
{
    return m_project_items;
}

std::size_t ActivityPanelModel::get_scroll_offset() const noexcept
{
    return m_scroll_offset;
}

bool ActivityPanelModel::is_expanded(const std::filesystem::path& path) const
{
    return std::find(m_expanded_paths.begin(), m_expanded_paths.end(), path) !=
        m_expanded_paths.end();
}

void ActivityPanelModel::toggle_expanded(const std::filesystem::path& path)
{
    const auto existing = std::find(m_expanded_paths.begin(), m_expanded_paths.end(), path);
    if (existing == m_expanded_paths.end())
    {
        m_expanded_paths.push_back(path);
    }
    else if (path != m_workspace_root)
    {
        m_expanded_paths.erase(existing);
    }
}

void ActivityPanelModel::append_directory(
    const std::filesystem::path& directory,
    std::size_t depth)
{
    if (m_project_items.size() >= maximum_tree_items)
    {
        return;
    }
    std::error_code error;
    std::vector<std::filesystem::directory_entry> children;
    for (std::filesystem::directory_iterator iterator{
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             error};
         !error && iterator != std::filesystem::directory_iterator{};
         iterator.increment(error))
    {
        children.push_back(*iterator);
    }
    std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
        std::error_code left_error;
        std::error_code right_error;
        const bool left_directory = left.is_directory(left_error) && !left.is_symlink(left_error);
        const bool right_directory = right.is_directory(right_error) && !right.is_symlink(right_error);
        if (left_directory != right_directory)
        {
            return left_directory;
        }
        return lowercase(left.path().filename().string()) <
            lowercase(right.path().filename().string());
    });

    for (const std::filesystem::directory_entry& child : children)
    {
        if (m_project_items.size() >= maximum_tree_items)
        {
            break;
        }
        error.clear();
        const bool directory_child = child.is_directory(error) && !child.is_symlink(error);
        const std::filesystem::path path = child.path().lexically_normal();
        const bool expanded = directory_child && is_expanded(path);
        m_project_items.push_back(ProjectTreeItem{
            .path = path,
            .label = path.filename().string(),
            .depth = depth,
            .directory = directory_child,
            .expanded = expanded,
        });
        if (expanded)
        {
            append_directory(path, depth + 1);
        }
    }
}

void ActivityPanelModel::rebuild_tree()
{
    m_project_items.clear();
    if (m_workspace_root.empty())
    {
        return;
    }
    std::error_code ec;
    std::erase_if(m_expanded_paths, [&](const std::filesystem::path& p) {
        return p != m_workspace_root && !std::filesystem::is_directory(p, ec);
    });
    m_project_items.push_back(ProjectTreeItem{
        .path = m_workspace_root,
        .label = m_workspace_root.filename().empty()
            ? m_workspace_root.string()
            : m_workspace_root.filename().string(),
        .depth = 0,
        .directory = true,
        .expanded = true,
    });
    append_directory(m_workspace_root, 1);
    m_scroll_offset = std::min(m_scroll_offset, m_project_items.empty()
        ? 0U
        : m_project_items.size() - 1);
}

SidebarIcon ActivityPanelModel::get_active_icon() const noexcept
{
    return m_active_icon;
}

void ActivityPanelModel::set_selected_path(std::optional<std::filesystem::path> path) noexcept
{
    m_selected_path = std::move(path);
}

const std::optional<std::filesystem::path>& ActivityPanelModel::get_selected_path() const noexcept
{
    return m_selected_path;
}

bool ActivityPanelModel::is_selected(const std::filesystem::path& path) const noexcept
{
    if (!m_selected_path.has_value())
    {
        return false;
    }
    return *m_selected_path == path;
}

std::filesystem::path ActivityPanelModel::get_target_directory_for_creation() const
{
    if (m_selected_path.has_value())
    {
        std::error_code ec;
        if (std::filesystem::is_directory(*m_selected_path, ec))
        {
            return *m_selected_path;
        }
        if (m_selected_path->has_parent_path())
        {
            return m_selected_path->parent_path();
        }
    }
    return m_workspace_root;
}

bool ActivityPanelModel::create_file(std::string_view relative_name, std::filesystem::path& out_path)
{
    if (relative_name.empty())
    {
        return false;
    }
    const std::filesystem::path target_dir = get_target_directory_for_creation();
    if (target_dir.empty())
    {
        return false;
    }

    std::filesystem::path full_path = target_dir / std::filesystem::path(relative_name);
    full_path = full_path.lexically_normal();

    std::error_code ec;
    if (full_path.has_parent_path())
    {
        std::filesystem::create_directories(full_path.parent_path(), ec);
    }

    if (!std::filesystem::exists(full_path, ec))
    {
        std::ofstream ofs(full_path, std::ios::out | std::ios::binary);
        if (!ofs.is_open())
        {
            return false;
        }
        ofs.close();
    }

    out_path = full_path;
    m_selected_path = full_path;

    // Ensure parent directories are in m_expanded_paths
    std::filesystem::path cur = full_path.parent_path();
    while (!cur.empty() && cur != m_workspace_root && cur != cur.parent_path())
    {
        if (std::find(m_expanded_paths.begin(), m_expanded_paths.end(), cur) == m_expanded_paths.end())
        {
            m_expanded_paths.push_back(cur);
        }
        cur = cur.parent_path();
    }

    rebuild_tree();
    return true;
}

bool ActivityPanelModel::create_directory(std::string_view relative_name, std::filesystem::path& out_path)
{
    if (relative_name.empty())
    {
        return false;
    }
    const std::filesystem::path target_dir = get_target_directory_for_creation();
    if (target_dir.empty())
    {
        return false;
    }

    std::filesystem::path full_path = target_dir / std::filesystem::path(relative_name);
    full_path = full_path.lexically_normal();

    std::error_code ec;
    if (!std::filesystem::create_directories(full_path, ec) && !std::filesystem::exists(full_path, ec))
    {
        return false;
    }

    out_path = full_path;
    m_selected_path = full_path;

    if (std::find(m_expanded_paths.begin(), m_expanded_paths.end(), full_path) == m_expanded_paths.end())
    {
        m_expanded_paths.push_back(full_path);
    }
    std::filesystem::path cur = full_path.parent_path();
    while (!cur.empty() && cur != m_workspace_root && cur != cur.parent_path())
    {
        if (std::find(m_expanded_paths.begin(), m_expanded_paths.end(), cur) == m_expanded_paths.end())
        {
            m_expanded_paths.push_back(cur);
        }
        cur = cur.parent_path();
    }

    rebuild_tree();
    return true;
}

bool ActivityPanelModel::rename_item(const std::filesystem::path& old_path, std::string_view new_name, std::filesystem::path& out_path)
{
    if (old_path.empty() || new_name.empty() || !old_path.has_parent_path())
    {
        return false;
    }

    const std::filesystem::path new_path = (old_path.parent_path() / std::filesystem::path(new_name)).lexically_normal();
    if (old_path == new_path)
    {
        out_path = new_path;
        return true;
    }

    std::error_code ec;
    std::filesystem::rename(old_path, new_path, ec);
    if (ec)
    {
        return false;
    }

    out_path = new_path;
    if (m_selected_path && *m_selected_path == old_path)
    {
        m_selected_path = new_path;
    }

    // Update expanded paths if it was a directory
    for (auto& exp : m_expanded_paths)
    {
        if (exp == old_path)
        {
            exp = new_path;
        }
        else if (exp.string().starts_with(old_path.string()))
        {
            const std::string suffix = exp.string().substr(old_path.string().length());
            exp = std::filesystem::path(new_path.string() + suffix);
        }
    }

    rebuild_tree();
    return true;
}

bool ActivityPanelModel::move_item(const std::filesystem::path& source_path, const std::filesystem::path& destination_directory, std::filesystem::path& out_path)
{
    if (source_path.empty() || destination_directory.empty() || source_path == m_workspace_root)
    {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(destination_directory, ec))
    {
        return false;
    }

    // Do not allow moving a directory into itself or its own subdirectories
    if (destination_directory.string().starts_with(source_path.string()))
    {
        return false;
    }

    const std::filesystem::path new_path = (destination_directory / source_path.filename()).lexically_normal();
    if (source_path == new_path)
    {
        out_path = new_path;
        return true;
    }

    std::filesystem::rename(source_path, new_path, ec);
    if (ec)
    {
        return false;
    }

    out_path = new_path;
    if (m_selected_path && *m_selected_path == source_path)
    {
        m_selected_path = new_path;
    }

    // Update expanded paths if it was a directory
    for (auto& exp : m_expanded_paths)
    {
        if (exp == source_path)
        {
            exp = new_path;
        }
        else if (exp.string().starts_with(source_path.string()))
        {
            const std::string suffix = exp.string().substr(source_path.string().length());
            exp = std::filesystem::path(new_path.string() + suffix);
        }
    }

    // Ensure destination directory is expanded
    if (std::find(m_expanded_paths.begin(), m_expanded_paths.end(), destination_directory) == m_expanded_paths.end())
    {
        m_expanded_paths.push_back(destination_directory);
    }

    rebuild_tree();
    return true;
}

bool ActivityPanelModel::delete_item(const std::filesystem::path& target_path)
{
    if (target_path.empty() || target_path == m_workspace_root)
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::remove_all(target_path, ec);
    if (ec)
    {
        return false;
    }

    if (m_selected_path && *m_selected_path == target_path)
    {
        m_selected_path.reset();
    }

    // Remove from expanded paths
    std::erase_if(m_expanded_paths, [&](const std::filesystem::path& p) {
        return p == target_path || p.string().starts_with(target_path.string());
    });

    rebuild_tree();
    return true;
}

} // namespace Zenvra::UI::Editor
