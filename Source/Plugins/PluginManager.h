#pragma once

#include "Plugins/Installer/DependencyResolver.h"
#include "Plugins/Installer/PluginInstaller.h"
#include "Plugins/Marketplace/MarketplaceClient.h"
#include "Plugins/Plugin.h"
#include "Plugins/PluginRegistry.h"
#include "Plugins/Toolchain/ToolManager.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins
{

class PluginManager
{
public:
    static PluginManager& instance() noexcept;

    void initialize(
        std::optional<std::filesystem::path> custom_home = std::nullopt,
        std::optional<std::filesystem::path> custom_bundled = std::nullopt);

    [[nodiscard]] PluginRegistry& get_registry() noexcept { return m_registry; }
    [[nodiscard]] const PluginRegistry& get_registry() const noexcept { return m_registry; }

    [[nodiscard]] Toolchain::ToolManager& get_tool_manager() noexcept { return Toolchain::ToolManager::instance(); }
    [[nodiscard]] const Toolchain::ToolManager& get_tool_manager() const noexcept { return Toolchain::ToolManager::instance(); }

    [[nodiscard]] Marketplace::MarketplaceClient& get_marketplace() noexcept { return m_marketplace; }
    [[nodiscard]] const Marketplace::MarketplaceClient& get_marketplace() const noexcept { return m_marketplace; }

    [[nodiscard]] Installer::DependencyResolver& get_dependency_resolver() noexcept { return m_resolver; }
    [[nodiscard]] const Installer::DependencyResolver& get_dependency_resolver() const noexcept { return m_resolver; }

    [[nodiscard]] Installer::PluginInstaller* get_installer() noexcept { return m_installer.get(); }

    Installer::InstallResult install_from_local(const std::filesystem::path& path);
    Installer::InstallResult install_from_git(
        const std::string& git_url,
        std::string_view branch = "",
        std::function<void(std::string_view)> log_callback = nullptr);

    bool uninstall_plugin(std::string_view plugin_id);
    bool enable_plugin(std::string_view plugin_id);
    bool disable_plugin(std::string_view plugin_id);

    [[nodiscard]] std::shared_ptr<Plugin> get_plugin(std::string_view plugin_id) const;
    [[nodiscard]] std::vector<std::shared_ptr<Plugin>> get_all_plugins() const;

    /// Discovers bundled and installed plugins from disk
    void scan_plugins();

    /// Activates a plugin's capabilities (Grammar rules, LSP server registrations)
    bool activate_plugin(std::shared_ptr<Plugin> plugin);

    /// Deactivates a plugin
    void deactivate_plugin(std::shared_ptr<Plugin> plugin);

    [[nodiscard]] std::vector<std::shared_ptr<Plugin>> get_installed_tool_plugins() const;
    [[nodiscard]] std::string get_active_tool_plugin_id() const;
    void set_active_tool_plugin_id(std::string_view plugin_id);
    [[nodiscard]] std::shared_ptr<Plugin> get_active_tool_plugin() const;

    [[nodiscard]] const std::filesystem::path& get_plugins_root() const noexcept { return m_plugins_root; }
    [[nodiscard]] const std::filesystem::path& get_zde_home() const noexcept { return m_zde_home; }
    [[nodiscard]] const std::filesystem::path& get_bundled_dir() const noexcept { return m_bundled_dir; }

private:
    PluginManager();
    ~PluginManager() = default;

    std::filesystem::path m_plugins_root;
    std::filesystem::path m_zde_home;
    std::filesystem::path m_bundled_dir;
    std::filesystem::path m_registry_file;

    PluginRegistry m_registry;
    Marketplace::MarketplaceClient m_marketplace;
    Installer::DependencyResolver m_resolver;
    std::unique_ptr<Installer::PluginInstaller> m_installer;

    std::string m_active_tool_plugin_id;
    bool m_initialized{false};
    mutable std::recursive_mutex m_mutex;
};

} // namespace Zenvra::Plugins
