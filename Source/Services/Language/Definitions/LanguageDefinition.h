#pragma once

#include "Services/Language/Definitions/EditorSettings.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Language::Definitions
{

struct ServerAssociation
{
    std::string default_server_id;
    std::vector<std::string> candidate_executables;
    std::vector<std::string> default_arguments;
    std::vector<std::string> root_markers = {".git"};
    nlohmann::json initialization_options = nlohmann::json::object();
    bool auto_install{false};

    [[nodiscard]] nlohmann::json to_json() const;
    static ServerAssociation from_json(const nlohmann::json& j);
};

struct LanguageDefinition
{
    std::string id;                                     // e.g. "cpp", "python", "rust"
    std::string display_name;                           // e.g. "C++", "Python", "Rust"
    std::vector<std::string> extensions;                // e.g. [".cpp", ".h", ".cxx"]
    std::vector<std::string> filenames;                 // e.g. ["CMakeLists.txt", "Dockerfile"]
    std::vector<std::string> first_line_patterns;       // e.g. ["^#!.*\\bpython[0-9.]*\\b"]
    std::vector<std::string> project_markers;           // e.g. ["Cargo.toml", "CMakeLists.txt"]
    std::vector<std::string> aliases;                   // e.g. ["c++", "cplusplus"]
    std::vector<std::string> framework_modes;           // e.g. ["react", "vue"]

    EditorSettings editor_settings;
    ServerAssociation server_association;
    bool is_builtin{true};
    std::string plugin_id;                              // Empty if built-in

    [[nodiscard]] bool matches_extension(std::string_view ext) const noexcept;
    [[nodiscard]] bool matches_filename(std::string_view fname) const noexcept;
    [[nodiscard]] bool matches_first_line(std::string_view first_line) const noexcept;

    [[nodiscard]] nlohmann::json to_json() const;
    static std::optional<LanguageDefinition> from_json(const nlohmann::json& j);
};

} // namespace Zenvra::Services::Language::Definitions

