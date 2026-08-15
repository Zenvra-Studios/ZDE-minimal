#pragma once

#include "Language/Syntax/GrammarRule.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Language::Syntax
{

class GrammarRegistry
{
public:
    static GrammarRegistry& instance() noexcept;

    /// Registers or updates a grammar rule.
    void register_grammar(GrammarRule rule);

    /// Loads a grammar definition from a JSON string.
    bool load_grammar_from_json(std::string_view json_content);

    /// Loads a grammar definition from a JSON file path.
    bool load_grammar_from_file(const std::filesystem::path& file_path);

    /// Loads all .json grammar files from a given directory (e.g. Assets/Grammars/).
    std::size_t load_grammars_from_directory(const std::filesystem::path& dir_path);

    /// Finds grammar matching the extension (e.g. ".rs", ".py", ".cpp").
    [[nodiscard]] const GrammarRule* get_grammar_for_extension(std::string_view extension) const noexcept;

    /// Finds grammar matching the given filename.
    [[nodiscard]] const GrammarRule* get_grammar_for_filename(std::string_view filename) const noexcept;

    /// Initializes default built-in grammars in memory as a fallback.
    void initialize_default_grammars();

private:
    GrammarRegistry();
    std::unordered_map<std::string, std::shared_ptr<GrammarRule>> m_grammars_by_name;
    std::unordered_map<std::string, std::shared_ptr<GrammarRule>> m_grammars_by_extension;
};

} // namespace Zenvra::Language::Syntax
