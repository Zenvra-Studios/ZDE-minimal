#include "PluginRuntime.h"

namespace Zenvra::Services::Plugins::Runtime
{

PluginRuntimeContext::PluginRuntimeContext(
    Loader::PluginPackage package,
    std::shared_ptr<Permissions::PermissionValidator> validator)
    : m_package(std::move(package)), m_validator(std::move(validator))
{
    if (m_validator)
    {
        m_validator->register_plugin_permissions(m_package.id, m_package.declared_permissions);
    }
}

bool PluginRuntimeContext::activate()
{
    if (m_state == PluginState::Active)
        return true;

    m_state = PluginState::Loading;
    // Activation hooks, load contributions
    m_state = PluginState::Active;
    return true;
}

void PluginRuntimeContext::deactivate()
{
    if (m_state == PluginState::Active)
    {
        m_state = PluginState::Deactivated;
    }
}

bool PluginRuntimeContext::can_execute_process() const
{
    return m_validator && m_validator->check_permission(m_package.id, Permissions::Permission::Process);
}

bool PluginRuntimeContext::can_read_filesystem() const
{
    return m_validator && m_validator->check_permission(m_package.id, Permissions::Permission::FilesystemRead);
}

bool PluginRuntimeContext::can_write_filesystem() const
{
    return m_validator && m_validator->check_permission(m_package.id, Permissions::Permission::FilesystemWrite);
}

bool PluginRuntimeContext::can_access_network() const
{
    return m_validator && m_validator->check_permission(m_package.id, Permissions::Permission::Network);
}

} // namespace Zenvra::Services::Plugins::Runtime

