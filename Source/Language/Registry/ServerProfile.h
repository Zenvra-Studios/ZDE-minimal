#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Zenvra::Language::Registry
{

struct ServerProfile
{
    std::string plugin_id;
    std::string language_id;
    std::vector<std::string> extensions;
    std::string executable_name;
    std::filesystem::path custom_executable_path;
    std::vector<std::string> default_args;
    std::vector<std::string> root_markers = {".git"};
    nlohmann::json initialization_options = nlohmann::json::object();

    [[nodiscard]] bool matches_extension(std::string_view ext) const noexcept
    {
        for (const auto& e : extensions)
        {
            if (e == ext) return true;
        }
        return false;
    }
};

} // namespace Zenvra::Language::Registry
