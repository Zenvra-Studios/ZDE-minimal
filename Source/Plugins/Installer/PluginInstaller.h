#pragma once

#include "Plugins/Plugin.h"
#include "Plugins/Toolchain/ToolManager.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Plugins::Installer
{

struct InstallResult
{
    bool success{false};
    std::string error_message;
    std::shared_ptr<Plugin> installed_plugin;
};

class PluginInstaller
{
public:
    PluginInstaller(
        std::filesystem::path plugins_dir,
        std::filesystem::path build_cache_dir,
        Toolchain::ToolManager* tool_manager = nullptr);

    [[nodiscard]] const std::filesystem::path& get_plugins_root() const noexcept { return m_plugins_root; }
    [[nodiscard]] const std::filesystem::path& get_installed_dir() const noexcept { return m_installed_dir; }
    [[nodiscard]] const std::filesystem::path& get_staging_dir() const noexcept { return m_staging_dir; }
    [[nodiscard]] std::filesystem::path get_target_path(std::string_view category, std::string_view plugin_id) const;

    /// Installs a plugin from a local folder using atomic staging
    [[nodiscard]] InstallResult install_from_local(
        const std::filesystem::path& source_path,
        std::string_view current_zde_version = "0.1.0");

    /// Installs a plugin from a Git repository using atomic staging & optional build
    [[nodiscard]] InstallResult install_from_git(
        const std::string& git_url,
        std::string_view branch = "",
        std::string_view current_zde_version = "0.1.0",
        std::function<void(std::string_view)> log_callback = nullptr);

    /// Uninstalls a plugin by removing its directory
    bool uninstall_plugin(std::string_view plugin_id);

    /// Validates a plugin directory without installing it
    [[nodiscard]] InstallResult validate_directory(const std::filesystem::path& dir_path) const;

private:
    std::filesystem::path m_plugins_root;
    std::filesystem::path m_installed_dir;
    std::filesystem::path m_staging_dir;
    std::filesystem::path m_build_cache_dir;
    [[maybe_unused]] Toolchain::ToolManager* m_tool_manager{nullptr};

    bool execute_source_build(
        const PluginManifest& manifest,
        const std::filesystem::path& source_dir,
        std::string& out_error,
        std::function<void(std::string_view)> log_callback);
};

} // namespace Zenvra::Plugins::Installer
