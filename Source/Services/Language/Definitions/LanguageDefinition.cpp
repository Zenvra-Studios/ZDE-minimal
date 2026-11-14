#include "Services/Language/Definitions/LanguageDefinition.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace Zenvra::Services::Language::Definitions
{

nlohmann::json ServerAssociation::to_json() const
{
    nlohmann::json j;
    j["defaultServerId"] = default_server_id;
    j["candidateExecutables"] = candidate_executables;
    j["defaultArguments"] = default_arguments;
    j["rootMarkers"] = root_markers;
    j["initializationOptions"] = initialization_options;
    j["autoInstall"] = auto_install;
    return j;
}

ServerAssociation ServerAssociation::from_json(const nlohmann::json& j)
{
    ServerAssociation sa;
    if (!j.is_object()) return sa;

    if (j.contains("defaultServerId") && j["defaultServerId"].is_string())
        sa.default_server_id = j["defaultServerId"].get<std::string>();
    if (j.contains("candidateExecutables") && j["candidateExecutables"].is_array())
        sa.candidate_executables = j["candidateExecutables"].get<std::vector<std::string>>();
    if (j.contains("defaultArguments") && j["defaultArguments"].is_array())
        sa.default_arguments = j["defaultArguments"].get<std::vector<std::string>>();
    if (j.contains("rootMarkers") && j["rootMarkers"].is_array())
        sa.root_markers = j["rootMarkers"].get<std::vector<std::string>>();
    if (j.contains("initializationOptions") && j["initializationOptions"].is_object())
        sa.initialization_options = j["initializationOptions"];
    if (j.contains("autoInstall") && j["autoInstall"].is_boolean())
        sa.auto_install = j["autoInstall"].get<bool>();

    return sa;
}

bool LanguageDefinition::matches_extension(std::string_view ext) const noexcept
{
    std::string lower_ext;
    lower_ext.reserve(ext.size());
    for (char c : ext) lower_ext += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& e : extensions)
    {
        std::string lower_e;
        lower_e.reserve(e.size());
        for (char c : e) lower_e += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_e == lower_ext) return true;
    }
    return false;
}

bool LanguageDefinition::matches_filename(std::string_view fname) const noexcept
{
    std::string lower_fname;
    lower_fname.reserve(fname.size());
    for (char c : fname) lower_fname += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& fn : filenames)
    {
        std::string lower_fn;
        lower_fn.reserve(fn.size());
        for (char c : fn) lower_fn += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_fn == lower_fname) return true;
    }
    return false;
}

bool LanguageDefinition::matches_first_line(std::string_view first_line) const noexcept
{
    if (first_line.empty() || first_line_patterns.empty()) return false;
    const std::string line_str(first_line);

    for (const auto& pattern : first_line_patterns)
    {
        try
        {
            std::regex reg(pattern, std::regex_constants::ECMAScript | std::regex_constants::icase);
            if (std::regex_search(line_str, reg))
            {
                return true;
            }
        }
        catch (...)
        {
            // Ignore malformed regex pattern safely
        }
    }
    return false;
}

nlohmann::json LanguageDefinition::to_json() const
{
    nlohmann::json j;
    j["id"] = id;
    j["displayName"] = display_name;
    j["extensions"] = extensions;
    j["filenames"] = filenames;
    j["firstLinePatterns"] = first_line_patterns;
    j["projectMarkers"] = project_markers;
    j["aliases"] = aliases;
    j["frameworkModes"] = framework_modes;
    j["editorSettings"] = editor_settings.to_json();
    j["serverAssociation"] = server_association.to_json();
    j["isBuiltin"] = is_builtin;
    j["pluginId"] = plugin_id;
    return j;
}

std::optional<LanguageDefinition> LanguageDefinition::from_json(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string())
    {
        return std::nullopt;
    }

    LanguageDefinition def;
    def.id = j["id"].get<std::string>();
    if (def.id.empty()) return std::nullopt;

    if (j.contains("displayName") && j["displayName"].is_string())
        def.display_name = j["displayName"].get<std::string>();
    else
        def.display_name = def.id;

    if (j.contains("extensions") && j["extensions"].is_array())
        def.extensions = j["extensions"].get<std::vector<std::string>>();
    if (j.contains("filenames") && j["filenames"].is_array())
        def.filenames = j["filenames"].get<std::vector<std::string>>();
    if (j.contains("firstLinePatterns") && j["firstLinePatterns"].is_array())
        def.first_line_patterns = j["firstLinePatterns"].get<std::vector<std::string>>();
    if (j.contains("projectMarkers") && j["projectMarkers"].is_array())
        def.project_markers = j["projectMarkers"].get<std::vector<std::string>>();
    if (j.contains("aliases") && j["aliases"].is_array())
        def.aliases = j["aliases"].get<std::vector<std::string>>();
    if (j.contains("frameworkModes") && j["frameworkModes"].is_array())
        def.framework_modes = j["frameworkModes"].get<std::vector<std::string>>();

    if (j.contains("editorSettings") && j["editorSettings"].is_object())
        def.editor_settings = EditorSettings::from_json(j["editorSettings"]);

    if (j.contains("serverAssociation") && j["serverAssociation"].is_object())
        def.server_association = ServerAssociation::from_json(j["serverAssociation"]);

    if (j.contains("isBuiltin") && j["isBuiltin"].is_boolean())
        def.is_builtin = j["isBuiltin"].get<bool>();

    if (j.contains("pluginId") && j["pluginId"].is_string())
        def.plugin_id = j["pluginId"].get<std::string>();

    return def;
}

} // namespace Zenvra::Services::Language::Definitions

