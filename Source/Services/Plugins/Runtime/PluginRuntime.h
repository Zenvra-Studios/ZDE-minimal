#pragma once

#include "Source/Services/Plugins/Permissions/PermissionValidator.h"
#include "Source/Services/Plugins/Loader/PackageLoader.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Zenvra::Services::Plugins::Runtime
{

enum class PluginState
{
    Unloaded,
    Loading,
    Active,
    Deactivated,
    Error
};

class PluginRuntimeContext
{
public:
    PluginRuntimeContext(
        Loader::PluginPackage package,
        std::shared_ptr<Permissions::PermissionValidator> validator);

    [[nodiscard]] const std::string& get_id() const noexcept { return m_package.id; }
    [[nodiscard]] PluginState get_state() const noexcept { return m_state; }
    [[nodiscard]] const Loader::PluginPackage& get_package() const noexcept { return m_package; }

    bool activate();
    void deactivate();

    [[nodiscard]] bool can_execute_process() const;
    [[nodiscard]] bool can_read_filesystem() const;
    [[nodiscard]] bool can_write_filesystem() const;
    [[nodiscard]] bool can_access_network() const;

private:
    Loader::PluginPackage m_package;
    std::shared_ptr<Permissions::PermissionValidator> m_validator;
    PluginState m_state{PluginState::Unloaded};
};

} // namespace Zenvra::Services::Plugins::Runtime

