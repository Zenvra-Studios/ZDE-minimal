#include "Plugins/Toolchain/ToolRegistry.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace Zenvra::Plugins::Toolchain
{

std::string_view tool_source_to_string(ToolSource source) noexcept
{
    switch (source)
    {
    case ToolSource::System: return "system";
    case ToolSource::Project: return "project";
    case ToolSource::Local: return "local";
    case ToolSource::Managed: return "managed";
    case ToolSource::Bundled: return "bundled";
    }
    return "system";
}

ToolSource tool_source_from_string(std::string_view str) noexcept
{
    if (str == "project") return ToolSource::Project;
    if (str == "local") return ToolSource::Local;
    if (str == "managed") return ToolSource::Managed;
    if (str == "bundled") return ToolSource::Bundled;
    return ToolSource::System;
}

std::string_view tool_status_to_string(ToolStatus status) noexcept
{
    switch (status)
    {
    case ToolStatus::Available: return "available";
    case ToolStatus::Missing: return "missing";
    case ToolStatus::Incompatible: return "incompatible";
    case ToolStatus::Error: return "error";
    }
    return "missing";
}

ToolStatus tool_status_from_string(std::string_view str) noexcept
{
    if (str == "available") return ToolStatus::Available;
    if (str == "incompatible") return ToolStatus::Incompatible;
    if (str == "error") return ToolStatus::Error;
    return ToolStatus::Missing;
}

void ToolRegistry::register_tool(ToolInfo tool)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tools[tool.id] = std::move(tool);
}

void ToolRegistry::unregister_tool(std::string_view tool_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tools.erase(std::string(tool_id));
}

std::optional<ToolInfo> ToolRegistry::get_tool(std::string_view tool_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tools.find(std::string(tool_id));
    if (it != m_tools.end())
    {
        return it->second;
    }
    return std::nullopt;
}

bool ToolRegistry::has_tool(std::string_view tool_id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tools.find(std::string(tool_id)) != m_tools.end();
}

std::vector<ToolInfo> ToolRegistry::get_all_tools() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToolInfo> result;
    result.reserve(m_tools.size());
    for (const auto& [_, tool] : m_tools)
    {
        result.push_back(tool);
    }
    return result;
}

void ToolRegistry::clear() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tools.clear();
}

bool ToolRegistry::load_from_disk(const std::filesystem::path& registry_file)
{
    std::ifstream stream(registry_file);
    if (!stream.is_open())
    {
        return false;
    }

    try
    {
        nlohmann::json root;
        stream >> root;

        if (root.contains("tools") && root["tools"].is_object())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tools.clear();
            for (const auto& [key, val] : root["tools"].items())
            {
                ToolInfo info;
                info.id = key;
                if (val.contains("name") && val["name"].is_string()) info.name = val["name"].get<std::string>();
                else info.name = key;

                if (val.contains("executable") && val["executable"].is_string())
                    info.executable_path = val["executable"].get<std::string>();

                if (val.contains("version") && val["version"].is_string())
                    info.version = val["version"].get<std::string>();

                if (val.contains("source") && val["source"].is_string())
                    info.source = tool_source_from_string(val["source"].get<std::string>());

                if (val.contains("status") && val["status"].is_string())
                    info.status = tool_status_from_string(val["status"].get<std::string>());

                if (val.contains("lastVerified") && val["lastVerified"].is_string())
                    info.last_verified = val["lastVerified"].get<std::string>();

                m_tools[key] = info;
            }
            return true;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ToolRegistry] Failed to parse " << registry_file << ": " << e.what() << '\n';
    }
    return false;
}

bool ToolRegistry::save_to_disk(const std::filesystem::path& registry_file) const
{
    std::error_code ec;
    std::filesystem::create_directories(registry_file.parent_path(), ec);

    std::ofstream stream(registry_file);
    if (!stream.is_open())
    {
        return false;
    }

    nlohmann::json root = nlohmann::json::object();
    nlohmann::json tools_obj = nlohmann::json::object();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [key, tool] : m_tools)
        {
            nlohmann::json t = nlohmann::json::object();
            t["id"] = tool.id;
            t["name"] = tool.name;
            t["executable"] = tool.executable_path.string();
            t["version"] = tool.version;
            t["source"] = tool_source_to_string(tool.source);
            t["status"] = tool_status_to_string(tool.status);
            t["lastVerified"] = tool.last_verified;
            tools_obj[key] = t;
        }
    }

    root["version"] = 1;
    root["tools"] = tools_obj;

    stream << root.dump(2);
    return true;
}

} // namespace Zenvra::Plugins::Toolchain
