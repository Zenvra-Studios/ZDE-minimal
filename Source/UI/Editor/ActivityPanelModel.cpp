#include "UI/Editor/ActivityPanelModel.h"

#include "UI/Editor/EditorFileSystem.h"

#include <algorithm>
#include <cctype>

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

bool should_hide_entry(const std::filesystem::path& path)
{
    const std::string name = path.filename().string();
    return name == ".git" || name == ".idea" || name == ".vs" ||
        name == ".vscode" || name == "build" || name.starts_with("cmake-build-");
}

} // namespace

bool ActivityPanelModel::initialize(const std::filesystem::path& workspace_root)
{
    std::error_code error;
    std::filesystem::path resolved_root = workspace_root;
    if (resolved_root.empty())
    {
        resolved_root = std::filesystem::current_path(error);
        if (error)
        {
            return false;
        }
        resolved_root = EditorFileSystem::find_project_root(resolved_root).value_or(resolved_root);
    }
    resolved_root = std::filesystem::weakly_canonical(resolved_root, error);
    if (error || !std::filesystem::is_directory(resolved_root, error))
    {
        return false;
    }

    m_workspace_root = std::move(resolved_root);
    m_expanded_paths.clear();
    m_expanded_paths.push_back(m_workspace_root);

    std::filesystem::path source_path = m_workspace_root / "Source";
    if (std::filesystem::is_directory(source_path, error))
    {
        m_expanded_paths.push_back(source_path);
        const std::filesystem::path platform_path = source_path / "Platform";
        if (std::filesystem::is_directory(platform_path, error))
        {
            m_expanded_paths.push_back(platform_path);
#if defined(_WIN32)
            const std::filesystem::path native_path = platform_path / "Win32";
#else
            const std::filesystem::path native_path = platform_path / "X11";
#endif
            if (std::filesystem::is_directory(native_path, error))
            {
                m_expanded_paths.push_back(native_path);
                const std::filesystem::path components_path = native_path / "Components";
                if (std::filesystem::is_directory(components_path, error))
                {
                    m_expanded_paths.push_back(components_path);
                }
            }
        }
    }
    const std::filesystem::path terminal_path = m_workspace_root / "Source" / "Terminal";
    if (std::filesystem::is_directory(terminal_path, error))
    {
        m_expanded_paths.push_back(terminal_path);
    }

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
    rebuild_tree();
    return true;
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
        m_scroll_offset = amount > m_scroll_offset ? 0 : m_scroll_offset - amount;
    }
    return m_scroll_offset != previous;
}

void ActivityPanelModel::set_scroll_offset(std::size_t offset) noexcept
{
    m_scroll_offset = std::min(offset, m_project_items.empty() ? 0U : m_project_items.size() - 1);
}

bool ActivityPanelModel::is_visible() const noexcept { return m_visible; }

void ActivityPanelModel::set_visible(bool visible) noexcept
{
    m_visible = visible;
}

bool ActivityPanelModel::is_active(SidebarIcon icon) const noexcept
{
    return m_visible && m_active_icon == icon;
}
SidebarIcon ActivityPanelModel::get_active_icon() const noexcept { return m_active_icon; }

std::string_view ActivityPanelModel::get_title() const noexcept
{
    switch (m_active_icon)
    {
    case SidebarIcon::Project: return "Explorer";
    case SidebarIcon::VersionControl: return "Source Control";
    case SidebarIcon::Search: return "Search";
    case SidebarIcon::Services: return "Services";
    case SidebarIcon::More: return "Tool Windows";
    case SidebarIcon::Run: return "Run and Debug";
    case SidebarIcon::Problems: return "Problems";
    case SidebarIcon::Terminal: return "Terminal";
    }
    return "Tool Window";
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
    }
    return {};
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
    }
    return {};
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
        if (!should_hide_entry(iterator->path()))
        {
            children.push_back(*iterator);
        }
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

} // namespace Zenvra::UI::Editor
