#include "Services/Language/Registry/LanguageValidator.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace Zenvra::Services::Language::Registry
{

std::vector<ValidationIssue> LanguageValidator::validate(
    const Definitions::LanguageDefinition& def,
    const std::vector<Definitions::LanguageDefinition>& existing_definitions)
{
    std::vector<ValidationIssue> issues;

    // 1. Language ID validation
    if (def.id.empty())
    {
        issues.push_back({
            .severity = ValidationIssue::Severity::Error,
            .language_id = def.id,
            .message = "Language ID must not be empty."
        });
    }
    else
    {
        const bool valid_id = std::all_of(def.id.begin(), def.id.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '+';
        });
        if (!valid_id)
        {
            issues.push_back({
                .severity = ValidationIssue::Severity::Error,
                .language_id = def.id,
                .message = "Language ID contains invalid characters. Use alphanumeric, '-', '_', or '+' only."
            });
        }
    }

    // 2. Extensions validation
    for (const auto& ext : def.extensions)
    {
        if (ext.empty() || ext[0] != '.')
        {
            issues.push_back({
                .severity = ValidationIssue::Severity::Warning,
                .language_id = def.id,
                .message = "Extension '" + ext + "' should start with a leading dot ('.')."
            });
        }
    }

    // 3. Duplicate checks against existing definitions
    for (const auto& existing : existing_definitions)
    {
        if (existing.id == def.id)
        {
            if (existing.is_builtin && !def.is_builtin)
            {
                issues.push_back({
                    .severity = ValidationIssue::Severity::Error,
                    .language_id = def.id,
                    .message = "Plugin language '" + def.id + "' cannot override protected built-in language definition."
                });
            }
        }
    }

    // 4. Regex pattern validation
    for (const auto& pattern : def.first_line_patterns)
    {
        try
        {
            std::regex test_regex(pattern, std::regex_constants::ECMAScript);
        }
        catch (const std::regex_error& e)
        {
            issues.push_back({
                .severity = ValidationIssue::Severity::Error,
                .language_id = def.id,
                .message = "Invalid first_line_pattern regex '" + pattern + "': " + e.what()
            });
        }
    }

    return issues;
}

} // namespace Zenvra::Services::Language::Registry

