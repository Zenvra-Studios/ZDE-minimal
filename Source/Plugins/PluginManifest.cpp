#include "Plugins/PluginManifest.h"

#include <fstream>
#include <iostream>
#include <regex>

namespace Zenvra::Plugins
{

namespace
{

bool parse_version_parts(std::string_view v, int& major, int& minor, int& patch)
{
    major = 0;
    minor = 0;
    patch = 0;
    if (v.empty()) return false;

    // Strip any leading 'v'
    if (v.front() == 'v' || v.front() == 'V')
    {
        v.remove_prefix(1);
    }

    std::string s(v);
    std::regex re(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+|x|\*))?)");
    std::smatch match;
    if (std::regex_search(s, match, re))
    {
        major = std::stoi(match[1]);
        if (match[2].matched)
        {
            minor = std::stoi(match[2]);
        }
        if (match[3].matched && match[3] != "x" && match[3] != "*")
        {
            patch = std::stoi(match[3]);
        }
        return true;
    }
    return false;
}

int compare_semver(std::string_view a, std::string_view b)
{
    int maj_a = 0, min_a = 0, pat_a = 0;
    int maj_b = 0, min_b = 0, pat_b = 0;
    parse_version_parts(a, maj_a, min_a, pat_a);
    parse_version_parts(b, maj_b, min_b, pat_b);

    if (maj_a != maj_b) return (maj_a < maj_b) ? -1 : 1;
    if (min_a != min_b) return (min_a < min_b) ? -1 : 1;
    if (pat_a != pat_b) return (pat_a < pat_b) ? -1 : 1;
    return 0;
}

std::string get_current_platform_string()
{
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
    #if defined(__arm64__) || defined(__aarch64__)
        return "macos-arm64";
    #else
        return "macos-x64";
    #endif
#elif defined(__linux__)
    return "linux-x64";
#else
    return "unknown";
#endif
}

} // namespace

std::optional<PluginManifest> PluginManifest::from_json(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        return std::nullopt;
    }

    PluginManifest manifest;

    if (j.contains("id") && j["id"].is_string())
    {
        manifest.m_id = j["id"].get<std::string>();
    }
    else if (j.contains("name") && j["name"].is_string())
    {
        std::string pub;
        if (j.contains("publisher") && j["publisher"].is_string())
        {
            pub = j["publisher"].get<std::string>();
        }
        else if (j.contains("publisher") && j["publisher"].is_object() && j["publisher"].contains("id") && j["publisher"]["id"].is_string())
        {
            pub = j["publisher"]["id"].get<std::string>();
        }
        manifest.m_id = pub.empty() ? j["name"].get<std::string>() : (pub + "." + j["name"].get<std::string>());
    }
    else
    {
        return std::nullopt; // ID is required
    }

    if (j.contains("name") && j["name"].is_string())
    {
        manifest.m_name = j["name"].get<std::string>();
    }
    else
    {
        manifest.m_name = manifest.m_id;
    }

    if (j.contains("version") && j["version"].is_string())
    {
        manifest.m_version = j["version"].get<std::string>();
    }

    if (j.contains("description") && j["description"].is_string())
    {
        manifest.m_description = j["description"].get<std::string>();
    }

    // Publisher
    if (j.contains("publisher"))
    {
        if (j["publisher"].is_object())
        {
            if (j["publisher"].contains("id") && j["publisher"]["id"].is_string())
                manifest.m_publisher_id = j["publisher"]["id"].get<std::string>();
            if (j["publisher"].contains("name") && j["publisher"]["name"].is_string())
                manifest.m_publisher_name = j["publisher"]["name"].get<std::string>();
        }
        else if (j["publisher"].is_string())
        {
            manifest.m_publisher_name = j["publisher"].get<std::string>();
            manifest.m_publisher_id = manifest.m_publisher_name;
        }
    }

    // ZDE version constraint
    if (j.contains("zde") && j["zde"].is_object())
    {
        const auto& zde = j["zde"];
        if (zde.contains("minimumVersion") && zde["minimumVersion"].is_string())
        {
            manifest.m_min_zde_version = zde["minimumVersion"].get<std::string>();
        }
        if (zde.contains("maximumVersion") && zde["maximumVersion"].is_string())
        {
            manifest.m_max_zde_version = zde["maximumVersion"].get<std::string>();
        }
    }

    // Platforms
    if (j.contains("platforms") && j["platforms"].is_array())
    {
        for (const auto& item : j["platforms"])
        {
            if (item.is_string())
            {
                manifest.m_platforms.push_back(item.get<std::string>());
            }
        }
    }

    // Capabilities
    if (j.contains("capabilities") && j["capabilities"].is_array())
    {
        for (const auto& item : j["capabilities"])
        {
            if (item.is_string())
            {
                manifest.m_capabilities.push_back(item.get<std::string>());
            }
        }
    }

    // Dependencies
    if (j.contains("dependencies"))
    {
        const auto& deps = j["dependencies"];
        if (deps.is_object())
        {
            if (deps.contains("plugins") && deps["plugins"].is_array())
            {
                for (const auto& p : deps["plugins"])
                {
                    PluginDependency dep;
                    if (p.is_object())
                    {
                        if (p.contains("id") && p["id"].is_string()) dep.id = p["id"].get<std::string>();
                        if (p.contains("version") && p["version"].is_string()) dep.version_requirement = p["version"].get<std::string>();
                        if (p.contains("optional") && p["optional"].is_boolean()) dep.optional = p["optional"].get<bool>();
                        if (!dep.id.empty()) manifest.m_plugin_dependencies.push_back(dep);
                    }
                    else if (p.is_string())
                    {
                        dep.id = p.get<std::string>();
                        manifest.m_plugin_dependencies.push_back(dep);
                    }
                }
            }

            if (deps.contains("tools") && deps["tools"].is_array())
            {
                for (const auto& t : deps["tools"])
                {
                    ToolDependency dep;
                    if (t.is_object())
                    {
                        if (t.contains("id") && t["id"].is_string()) dep.id = t["id"].get<std::string>();
                        if (t.contains("version") && t["version"].is_string()) dep.version_requirement = t["version"].get<std::string>();
                        if (t.contains("optional") && t["optional"].is_boolean()) dep.optional = t["optional"].get<bool>();
                        if (t.contains("description") && t["description"].is_string()) dep.description = t["description"].get<std::string>();
                        if (!dep.id.empty()) manifest.m_tool_dependencies.push_back(dep);
                    }
                    else if (t.is_string())
                    {
                        dep.id = t.get<std::string>();
                        manifest.m_tool_dependencies.push_back(dep);
                    }
                }
            }
        }
        else if (deps.is_array())
        {
            // Legacy / simple list of plugin or tool dependencies
            for (const auto& d : deps)
            {
                if (d.is_object() && d.contains("id"))
                {
                    PluginDependency dep;
                    dep.id = d["id"].get<std::string>();
                    if (d.contains("version")) dep.version_requirement = d["version"].get<std::string>();
                    manifest.m_plugin_dependencies.push_back(dep);
                }
            }
        }
    }

    // Build configuration
    if (j.contains("build") && j["build"].is_object())
    {
        PluginBuildInfo binfo;
        const auto& b = j["build"];
        if (b.contains("system") && b["system"].is_string())
        {
            binfo.system = b["system"].get<std::string>();
        }
        if (b.contains("output") && b["output"].is_string())
        {
            binfo.output_directory = b["output"].get<std::string>();
        }
        else if (b.contains("output") && b["output"].is_object() && b["output"].contains("directory"))
        {
            binfo.output_directory = b["output"]["directory"].get<std::string>();
        }
        manifest.m_build_info = binfo;
    }

    if (j.contains("category") && j["category"].is_string())
    {
        manifest.m_category = j["category"].get<std::string>();
    }
    if (manifest.m_category.empty())
    {
        manifest.m_category = manifest.deduce_category();
    }

    return manifest;
}

std::string PluginManifest::deduce_category() const
{
    if (!m_category.empty())
    {
        return m_category;
    }

    std::string lower_id = m_id;
    std::string lower_name = m_name;
    std::string lower_desc = m_description;
    std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(lower_desc.begin(), lower_desc.end(), lower_desc.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& cap : m_capabilities)
    {
        std::string lower_cap = cap;
        std::transform(lower_cap.begin(), lower_cap.end(), lower_cap.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower_cap == "language" || lower_cap == "lsp" || lower_cap == "languageserver")
        {
            return "lsp";
        }
        if (lower_cap == "emulator" || lower_cap == "virtualmachine" || lower_cap == "debugger")
        {
            return "emulators";
        }
        if (lower_cap == "theme")
        {
            return "themes";
        }
        if (lower_cap == "tool" || lower_cap == "tools")
        {
            return "tools";
        }
    }

    if (lower_id.find("lsp") != std::string::npos || lower_id.find("clangd") != std::string::npos ||
        lower_id.find("analyzer") != std::string::npos || lower_id.find("pyright") != std::string::npos ||
        lower_id.find("gopls") != std::string::npos || lower_id.find("zls") != std::string::npos ||
        lower_id.find("omnisharp") != std::string::npos || lower_name.find("lsp") != std::string::npos ||
        lower_name.find("language") != std::string::npos || lower_desc.find("language server") != std::string::npos)
    {
        return "lsp";
    }

    if (lower_id.find("emulator") != std::string::npos || lower_id.find("qemu") != std::string::npos ||
        lower_id.find("bochs") != std::string::npos || lower_name.find("emulator") != std::string::npos ||
        lower_desc.find("emulator") != std::string::npos)
    {
        return "emulators";
    }

    if (lower_id.find("theme") != std::string::npos || lower_name.find("theme") != std::string::npos)
    {
        return "themes";
    }

    if (lower_id.find("git") != std::string::npos || lower_id.find("docker") != std::string::npos ||
        lower_id.find("format") != std::string::npos || lower_id.find("lint") != std::string::npos)
    {
        return "tools";
    }

    return "general";
}

std::optional<PluginManifest> PluginManifest::from_file(const std::filesystem::path& file_path)
{
    std::ifstream stream(file_path);
    if (!stream.is_open())
    {
        return std::nullopt;
    }

    try
    {
        nlohmann::json j;
        stream >> j;
        return from_json(j);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[PluginManifest] Error parsing " << file_path << ": " << e.what() << '\n';
        return std::nullopt;
    }
}

bool PluginManifest::has_capability(std::string_view cap) const noexcept
{
    for (const auto& c : m_capabilities)
    {
        if (c == cap) return true;
    }
    return false;
}

bool PluginManifest::is_compatible_with_zde(std::string_view current_version) const noexcept
{
    if (!m_min_zde_version.empty())
    {
        if (compare_semver(current_version, m_min_zde_version) < 0)
        {
            return false;
        }
    }

    if (!m_max_zde_version.empty() && m_max_zde_version != "null")
    {
        if (compare_semver(current_version, m_max_zde_version) > 0)
        {
            return false;
        }
    }

    return true;
}

bool PluginManifest::is_platform_supported(std::string_view current_platform) const noexcept
{
    if (m_platforms.empty())
    {
        return true; // No platform restrictions declared
    }

    std::string plat = current_platform.empty() ? get_current_platform_string() : std::string(current_platform);
    for (const auto& p : m_platforms)
    {
        if (p == plat || p == "all" || p == "any")
        {
            return true;
        }
    }
    return false;
}

nlohmann::json PluginManifest::to_json() const
{
    nlohmann::json j = nlohmann::json::object();
    j["id"] = m_id;
    j["name"] = m_name;
    j["version"] = m_version;
    j["description"] = m_description;
    j["category"] = deduce_category();

    nlohmann::json pub = nlohmann::json::object();
    pub["id"] = m_publisher_id;
    pub["name"] = m_publisher_name;
    j["publisher"] = pub;

    nlohmann::json zde = nlohmann::json::object();
    zde["minimumVersion"] = m_min_zde_version;
    if (!m_max_zde_version.empty()) zde["maximumVersion"] = m_max_zde_version;
    j["zde"] = zde;

    j["platforms"] = m_platforms;
    j["capabilities"] = m_capabilities;

    nlohmann::json deps = nlohmann::json::object();
    nlohmann::json plugins_arr = nlohmann::json::array();
    for (const auto& p : m_plugin_dependencies)
    {
        plugins_arr.push_back({
            {"id", p.id},
            {"version", p.version_requirement},
            {"optional", p.optional}
        });
    }
    deps["plugins"] = plugins_arr;

    nlohmann::json tools_arr = nlohmann::json::array();
    for (const auto& t : m_tool_dependencies)
    {
        tools_arr.push_back({
            {"id", t.id},
            {"version", t.version_requirement},
            {"optional", t.optional},
            {"description", t.description}
        });
    }
    deps["tools"] = tools_arr;
    j["dependencies"] = deps;

    return j;
}

} // namespace Zenvra::Plugins
