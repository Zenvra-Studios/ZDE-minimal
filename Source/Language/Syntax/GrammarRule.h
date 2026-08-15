#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace Zenvra::Language::Syntax
{

struct GrammarRule
{
    std::string name;
    std::vector<std::string> extensions;
    std::string line_comment;
    std::string block_comment_start;
    std::string block_comment_end;
    std::unordered_set<std::string> keywords;
    std::unordered_set<std::string> types;
    std::vector<std::string> operators;
    std::vector<std::string> string_delimiters = {"\"", "'"};
    bool supports_preprocessor = false;

    [[nodiscard]] bool is_keyword(std::string_view word) const noexcept
    {
        return keywords.contains(std::string(word));
    }

    [[nodiscard]] bool is_type(std::string_view word) const noexcept
    {
        return types.contains(std::string(word));
    }
};

} // namespace Zenvra::Language::Syntax
