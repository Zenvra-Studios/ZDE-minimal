#pragma once

#include "Services/Language/Definitions/LanguageDefinition.h"
#include "Services/Language/Detection/DetectionRule.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Language::Detection
{

class LanguageDetector
{
public:
    LanguageDetector() = default;

    /// Sets an explicit user association: pattern (e.g. "*.custom" or "Dockerfile*") -> language_id
    void set_user_association(std::string pattern, std::string language_id);
    void remove_user_association(std::string_view pattern);
    void clear_user_associations();

    /// Primary language detection API
    [[nodiscard]] DetectionResult detect(
        const std::filesystem::path& file_path,
        std::string_view file_content = {},
        const std::filesystem::path& workspace_root = {},
        const std::vector<Definitions::LanguageDefinition>& available_languages = {}) const;

    /// Checks if a file is an STL header (e.g. <vector>, <string>, <iostream>) without extension
    [[nodiscard]] static bool is_stl_header(std::string_view filename, const std::filesystem::path& full_path = {}) noexcept;

private:
    [[nodiscard]] static std::string extract_first_line(std::string_view content) noexcept;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::string> m_user_associations;
};

} // namespace Zenvra::Services::Language::Detection

