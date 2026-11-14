#pragma once

#include "PermissionSet.h"

#include <string>
#include <unordered_map>

namespace Zenvra::Services::Plugins::Permissions
{

class PermissionValidator
{
public:
    PermissionValidator() = default;

    void register_plugin_permissions(const std::string& plugin_id, const PermissionSet& declared_permissions);

    void grant_user_permissions(const std::string& plugin_id, const PermissionSet& granted_permissions);

    [[nodiscard]] bool check_permission(const std::string& plugin_id, Permission permission) const;

    [[nodiscard]] PermissionSet get_declared_permissions(const std::string& plugin_id) const;

    [[nodiscard]] PermissionSet get_effective_permissions(const std::string& plugin_id) const;

private:
    std::unordered_map<std::string, PermissionSet> m_declared;
    std::unordered_map<std::string, PermissionSet> m_granted;
};

} // namespace Zenvra::Services::Plugins::Permissions

