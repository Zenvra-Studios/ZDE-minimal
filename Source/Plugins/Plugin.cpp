#include "Plugins/Plugin.h"

#include <system_error>

namespace Zenvra::Plugins
{

std::string_view plugin_source_to_string(PluginSource source) noexcept
{
    switch (source)
    {
    case PluginSource::Bundled: return "bundled";
    case PluginSource::Git: return "git";
    case PluginSource::Local: return "local";
    case PluginSource::Marketplace: return "marketplace";
    }
    return "local";
}

PluginSource plugin_source_from_string(std::string_view str) noexcept
{
    if (str == "bundled") return PluginSource::Bundled;
    if (str == "git") return PluginSource::Git;
    if (str == "marketplace") return PluginSource::Marketplace;
    return PluginSource::Local;
}

std::string_view plugin_state_to_string(PluginState state) noexcept
{
    switch (state)
    {
    case PluginState::Discovered: return "discovered";
    case PluginState::Validated: return "validated";
    case PluginState::Installed: return "installed";
    case PluginState::Registered: return "registered";
    case PluginState::Enabled: return "enabled";
    case PluginState::Disabled: return "disabled";
    case PluginState::Failed: return "failed";
    }
    return "discovered";
}

PluginState plugin_state_from_string(std::string_view str) noexcept
{
    if (str == "enabled") return PluginState::Enabled;
    if (str == "disabled") return PluginState::Disabled;
    if (str == "registered") return PluginState::Registered;
    if (str == "installed") return PluginState::Installed;
    if (str == "validated") return PluginState::Validated;
    if (str == "failed") return PluginState::Failed;
    return PluginState::Discovered;
}

Plugin::Plugin(PluginManifest manifest, std::filesystem::path install_path, PluginSource source)
    : m_manifest(std::move(manifest))
    , m_install_path(std::move(install_path))
    , m_source(source)
    , m_state(PluginState::Discovered)
{
}

std::vector<std::filesystem::path> Plugin::get_grammar_files() const
{
    std::vector<std::filesystem::path> results;
    std::filesystem::path dir = m_install_path / "grammar";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                results.push_back(entry.path());
            }
        }
    }
    return results;
}

std::vector<std::filesystem::path> Plugin::get_language_files() const
{
    std::vector<std::filesystem::path> results;
    std::filesystem::path dir = m_install_path / "language";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                results.push_back(entry.path());
            }
        }
    }
    return results;
}

std::vector<std::filesystem::path> Plugin::get_lsp_files() const
{
    std::vector<std::filesystem::path> results;
    std::filesystem::path dir = m_install_path / "lsp";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                results.push_back(entry.path());
            }
        }
    }
    return results;
}

std::vector<std::filesystem::path> Plugin::get_theme_files() const
{
    std::vector<std::filesystem::path> results;
    std::error_code ec;

    // 1. Direct themes/ directory
    std::filesystem::path dir = m_install_path / "themes";
    if (std::filesystem::is_directory(dir, ec))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                results.push_back(entry.path());
            }
        }
    }

    // 2. Direct theme.json in root
    std::filesystem::path root_theme = m_install_path / "theme.json";
    if (std::filesystem::is_regular_file(root_theme, ec))
    {
        if (std::find(results.begin(), results.end(), root_theme) == results.end())
        {
            results.push_back(root_theme);
        }
    }

    return results;
}

} // namespace Zenvra::Plugins

