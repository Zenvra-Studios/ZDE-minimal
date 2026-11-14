#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Plugins::Permissions
{

enum class Permission : uint32_t
{
    None = 0,
    Process = 1 << 0,          // Execute external processes
    FilesystemRead = 1 << 1,   // Read permitted files
    FilesystemWrite = 1 << 2,  // Write permitted files
    Network = 1 << 3,          // Network access
    WorkspaceRead = 1 << 4,    // Access workspace content
    WorkspaceWrite = 1 << 5    // Modify workspace content
};

inline Permission operator|(Permission a, Permission b)
{
    return static_cast<Permission>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline Permission operator&(Permission a, Permission b)
{
    return static_cast<Permission>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

class PermissionSet
{
public:
    PermissionSet() = default;
    explicit PermissionSet(Permission perms) : m_mask(static_cast<uint32_t>(perms)) {}

    void grant(Permission perm) { m_mask |= static_cast<uint32_t>(perm); }
    void revoke(Permission perm) { m_mask &= ~static_cast<uint32_t>(perm); }
    [[nodiscard]] bool has(Permission perm) const { return (m_mask & static_cast<uint32_t>(perm)) != 0; }

    [[nodiscard]] uint32_t get_mask() const noexcept { return m_mask; }

    [[nodiscard]] static Permission parse_permission(std::string_view name);
    [[nodiscard]] static std::string to_string(Permission perm);
    [[nodiscard]] std::vector<std::string> to_string_list() const;

private:
    uint32_t m_mask{0};
};

} // namespace Zenvra::Services::Plugins::Permissions

