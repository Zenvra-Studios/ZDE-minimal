#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins
{

struct PluginDependency
{
    std::string id;
    std::string version_requirement; // e.g. ">=1.0.0"
    bool optional{false};
};

struct ToolDependency
{
    std::string id;                  // e.g. "clangd", "rust-analyzer"
    std::string version_requirement; // e.g. ">=17.0.0"
    bool optional{false};
    std::string description;
};

struct PluginBuildInfo
{
    std::string system;              // e.g. "cmake", "cargo"
    std::vector<std::string> requirements;
    std::vector<std::string> configure_args;
    std::vector<std::string> targets;
    std::string output_directory;
};

class PluginManifest
{
public:
    PluginManifest() = default;

    static std::optional<PluginManifest> from_json(const nlohmann::json& j);
    static std::optional<PluginManifest> from_file(const std::filesystem::path& file_path);

    [[nodiscard]] const std::string& get_id() const noexcept { return m_id; }
    [[nodiscard]] const std::string& get_name() const noexcept { return m_name; }
    [[nodiscard]] const std::string& get_version() const noexcept { return m_version; }
    [[nodiscard]] const std::string& get_publisher_id() const noexcept { return m_publisher_id; }
    [[nodiscard]] const std::string& get_publisher_name() const noexcept { return m_publisher_name; }
    [[nodiscard]] const std::string& get_description() const noexcept { return m_description; }

    [[nodiscard]] const std::string& get_category() const noexcept { return m_category; }
    [[nodiscard]] std::string deduce_category() const;
    void set_category(std::string cat) { m_category = std::move(cat); }

    [[nodiscard]] const std::string& get_minimum_zde_version() const noexcept { return m_min_zde_version; }
    [[nodiscard]] const std::string& get_maximum_zde_version() const noexcept { return m_max_zde_version; }

    [[nodiscard]] const std::vector<std::string>& get_platforms() const noexcept { return m_platforms; }
    [[nodiscard]] const std::vector<std::string>& get_capabilities() const noexcept { return m_capabilities; }

    [[nodiscard]] const std::vector<PluginDependency>& get_plugin_dependencies() const noexcept { return m_plugin_dependencies; }
    [[nodiscard]] const std::vector<ToolDependency>& get_tool_dependencies() const noexcept { return m_tool_dependencies; }

    [[nodiscard]] const std::optional<PluginBuildInfo>& get_build_info() const noexcept { return m_build_info; }

    [[nodiscard]] bool has_capability(std::string_view cap) const noexcept;
    [[nodiscard]] bool is_compatible_with_zde(std::string_view current_version) const noexcept;
    [[nodiscard]] bool is_platform_supported(std::string_view current_platform) const noexcept;

    [[nodiscard]] nlohmann::json to_json() const;

    [[nodiscard]] const std::string& get_publisher() const noexcept { return m_publisher_name.empty() ? m_publisher_id : m_publisher_name; }

    void set_id(std::string id) { m_id = std::move(id); }
    void set_name(std::string name) { m_name = std::move(name); }
    void set_version(std::string version) { m_version = std::move(version); }
    void set_description(std::string desc) { m_description = std::move(desc); }
    void set_publisher(std::string publisher) { m_publisher_name = std::move(publisher); }
    void set_publisher_id(std::string id) { m_publisher_id = std::move(id); }
    void set_publisher_name(std::string name) { m_publisher_name = std::move(name); }

private:
    std::string m_id;
    std::string m_name;
    std::string m_version{"0.1.0"};
    std::string m_publisher_id;
    std::string m_publisher_name;
    std::string m_description;
    std::string m_category;
    std::string m_min_zde_version{"0.1.0"};
    std::string m_max_zde_version;
    std::vector<std::string> m_platforms;
    std::vector<std::string> m_capabilities;
    std::vector<PluginDependency> m_plugin_dependencies;
    std::vector<ToolDependency> m_tool_dependencies;
    std::optional<PluginBuildInfo> m_build_info;
};

} // namespace Zenvra::Plugins
