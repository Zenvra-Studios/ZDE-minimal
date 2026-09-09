#pragma once

#include "Plugins/PluginManifest.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins
{

enum class PluginSource
{
    Bundled,
    Git,
    Local,
    Marketplace
};

enum class PluginState
{
    Discovered,
    Validated,
    Installed,
    Registered,
    Enabled,
    Disabled,
    Failed
};

std::string_view plugin_source_to_string(PluginSource source) noexcept;
PluginSource plugin_source_from_string(std::string_view str) noexcept;

std::string_view plugin_state_to_string(PluginState state) noexcept;
PluginState plugin_state_from_string(std::string_view str) noexcept;

class Plugin
{
public:
    Plugin(PluginManifest manifest, std::filesystem::path install_path, PluginSource source = PluginSource::Local);

    [[nodiscard]] const std::string& get_id() const noexcept { return m_manifest.get_id(); }
    [[nodiscard]] const std::string& get_name() const noexcept { return m_manifest.get_name(); }
    [[nodiscard]] const std::string& get_version() const noexcept { return m_manifest.get_version(); }
    [[nodiscard]] const std::string& get_description() const noexcept { return m_manifest.get_description(); }
    [[nodiscard]] const PluginManifest& get_manifest() const noexcept { return m_manifest; }
    [[nodiscard]] const std::filesystem::path& get_install_path() const noexcept { return m_install_path; }

    [[nodiscard]] PluginSource get_source() const noexcept { return m_source; }
    void set_source(PluginSource source) noexcept { m_source = source; }

    [[nodiscard]] PluginState get_state() const noexcept { return m_state; }
    void set_state(PluginState state) noexcept { m_state = state; }

    [[nodiscard]] bool is_enabled() const noexcept { return m_state == PluginState::Enabled; }
    void set_enabled(bool enabled) noexcept
    {
        m_state = enabled ? PluginState::Enabled : PluginState::Disabled;
    }

    [[nodiscard]] std::vector<std::filesystem::path> get_grammar_files() const;
    [[nodiscard]] std::vector<std::filesystem::path> get_language_files() const;
    [[nodiscard]] std::vector<std::filesystem::path> get_lsp_files() const;

private:
    PluginManifest m_manifest;
    std::filesystem::path m_install_path;
    PluginSource m_source{PluginSource::Local};
    PluginState m_state{PluginState::Discovered};
};

} // namespace Zenvra::Plugins
