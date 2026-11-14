#include "PermissionValidator.h"
#include <algorithm>

namespace Zenvra::Services::Plugins::Permissions
{

Permission PermissionSet::parse_permission(std::string_view name)
{
    if (name == "process") return Permission::Process;
    if (name == "filesystem.read") return Permission::FilesystemRead;
    if (name == "filesystem.write") return Permission::FilesystemWrite;
    if (name == "network") return Permission::Network;
    if (name == "workspace.read") return Permission::WorkspaceRead;
    if (name == "workspace.write") return Permission::WorkspaceWrite;
    return Permission::None;
}

std::string PermissionSet::to_string(Permission perm)
{
    switch (perm)
    {
    case Permission::Process: return "process";
    case Permission::FilesystemRead: return "filesystem.read";
    case Permission::FilesystemWrite: return "filesystem.write";
    case Permission::Network: return "network";
    case Permission::WorkspaceRead: return "workspace.read";
    case Permission::WorkspaceWrite: return "workspace.write";
    default: return "none";
    }
}

std::vector<std::string> PermissionSet::to_string_list() const
{
    std::vector<std::string> list;
    if (has(Permission::Process)) list.push_back("process");
    if (has(Permission::FilesystemRead)) list.push_back("filesystem.read");
    if (has(Permission::FilesystemWrite)) list.push_back("filesystem.write");
    if (has(Permission::Network)) list.push_back("network");
    if (has(Permission::WorkspaceRead)) list.push_back("workspace.read");
    if (has(Permission::WorkspaceWrite)) list.push_back("workspace.write");
    return list;
}

void PermissionValidator::register_plugin_permissions(
    const std::string& plugin_id,
    const PermissionSet& declared_permissions)
{
    m_declared[plugin_id] = declared_permissions;
    // By default, safe permissions (filesystem.read, workspace.read) are auto-granted
    PermissionSet auto_granted;
    if (declared_permissions.has(Permission::FilesystemRead))
        auto_granted.grant(Permission::FilesystemRead);
    if (declared_permissions.has(Permission::WorkspaceRead))
        auto_granted.grant(Permission::WorkspaceRead);
    m_granted[plugin_id] = auto_granted;
}

void PermissionValidator::grant_user_permissions(
    const std::string& plugin_id,
    const PermissionSet& granted_permissions)
{
    auto it = m_declared.find(plugin_id);
    if (it != m_declared.end())
    {
        // Only grant permissions that were actually declared by the plugin
        uint32_t allowed = it->second.get_mask() & granted_permissions.get_mask();
        m_granted[plugin_id] = PermissionSet(static_cast<Permission>(allowed));
    }
}

bool PermissionValidator::check_permission(const std::string& plugin_id, Permission permission) const
{
    auto it = m_granted.find(plugin_id);
    if (it == m_granted.end())
        return false;

    return it->second.has(permission);
}

PermissionSet PermissionValidator::get_declared_permissions(const std::string& plugin_id) const
{
    auto it = m_declared.find(plugin_id);
    if (it != m_declared.end())
        return it->second;
    return PermissionSet{};
}

PermissionSet PermissionValidator::get_effective_permissions(const std::string& plugin_id) const
{
    auto it = m_granted.find(plugin_id);
    if (it != m_granted.end())
        return it->second;
    return PermissionSet{};
}

} // namespace Zenvra::Services::Plugins::Permissions

