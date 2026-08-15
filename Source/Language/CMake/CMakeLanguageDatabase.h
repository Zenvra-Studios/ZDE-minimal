#pragma once

#include "Language/Protocol/LspTypes.h"
#include <optional>
#include <string_view>
#include <vector>

namespace Zenvra::Language::CMake
{

class CMakeLanguageDatabase
{
public:
    static CMakeLanguageDatabase& instance() noexcept;

    [[nodiscard]] std::vector<Protocol::CompletionItem> get_all_completions() const;
    [[nodiscard]] std::vector<Protocol::CompletionItem> get_completions_for_context(
        std::string_view current_line,
        std::size_t caret_column) const;

    [[nodiscard]] std::optional<Protocol::Hover> find_hover(std::string_view symbol_name) const;
    [[nodiscard]] std::optional<Protocol::SignatureHelp> find_signature_help(std::string_view command_name) const;

private:
    CMakeLanguageDatabase();
    void initialize_database();

    std::vector<Protocol::CompletionItem> m_all_completions;
};

} // namespace Zenvra::Language::CMake
