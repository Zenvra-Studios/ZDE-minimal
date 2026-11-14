#pragma once

#include "Services/Language/Definitions/LanguageDefinition.h"
#include "Services/Language/Detection/LanguageDetector.h"
#include "Services/Language/Registry/LanguageValidator.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Language::Registry
{

class LanguageRegistry
{
public:
    static LanguageRegistry& instance() noexcept;

    /// Initializes built-in languages from Assets/LanguageDefinitions or embedded defaults
    void initialize(const std::filesystem::path& assets_dir = {});

    /// Registration APIs
    bool register_language(Definitions::LanguageDefinition definition);
    bool unregister_language(std::string_view language_id);
    void unregister_languages_for_plugin(std::string_view plugin_id);

    /// Lookups
    [[nodiscard]] const Definitions::LanguageDefinition* find_by_id(std::string_view language_id) const noexcept;
    [[nodiscard]] const Definitions::LanguageDefinition* find_by_extension(std::string_view extension) const noexcept;
    [[nodiscard]] const Definitions::LanguageDefinition* find_by_filename(std::string_view filename) const noexcept;
    [[nodiscard]] std::vector<const Definitions::LanguageDefinition*> get_all_languages() const;
    [[nodiscard]] bool has_language(std::string_view language_id) const noexcept;

    /// Unified detection entry point (FR-02)
    [[nodiscard]] Detection::DetectionResult detect_language(
        const std::filesystem::path& file_path,
        std::string_view file_content = {},
        const std::filesystem::path& workspace_root = {}) const;

    /// Editor settings resolution (FR-01)
    [[nodiscard]] Definitions::EditorSettings get_editor_settings(
        std::string_view language_id_or_extension) const noexcept;

    /// Server association resolution (FR-01)
    [[nodiscard]] std::optional<Definitions::ServerAssociation> get_server_association(
        std::string_view language_id) const noexcept;

    /// Load single definition from file
    bool load_from_file(const std::filesystem::path& file_path);

    /// Load definitions from directory
    std::size_t load_definitions_from_directory(const std::filesystem::path& dir_path);

    /// Detector access
    [[nodiscard]] Detection::LanguageDetector& detector() noexcept { return m_detector; }
    [[nodiscard]] const Detection::LanguageDetector& detector() const noexcept { return m_detector; }

    void clear() noexcept;

private:
    LanguageRegistry();
    ~LanguageRegistry() = default;

    void register_builtin_defaults();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, Definitions::LanguageDefinition> m_definitions; // ID -> Definition
    std::unordered_map<std::string, std::string> m_extension_to_id;                // Extension -> ID
    std::unordered_map<std::string, std::string> m_filename_to_id;                 // Filename (lower) -> ID
    Detection::LanguageDetector m_detector;
    bool m_initialized{false};
};

} // namespace Zenvra::Services::Language::Registry

