#pragma once

#include <algorithm>
#include <cctype>
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
    std::unordered_set<std::string> variables;
    std::vector<std::string> operators;
    std::vector<std::string> string_delimiters = {"\"", "'"};
    bool supports_preprocessor = false;
    bool case_insensitive = false;

    [[nodiscard]] bool is_keyword(std::string_view word) const noexcept
    {
        if (keywords.contains(std::string(word))) return true;
        if (case_insensitive)
        {
            std::string lower(word);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (keywords.contains(lower)) return true;
            std::string upper(word);
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return keywords.contains(upper);
        }
        return false;
    }

    [[nodiscard]] bool is_type(std::string_view word) const noexcept
    {
        if (types.contains(std::string(word))) return true;
        if (case_insensitive)
        {
            std::string lower(word);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (types.contains(lower)) return true;
            std::string upper(word);
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return types.contains(upper);
        }
        return false;
    }

    [[nodiscard]] bool is_variable(std::string_view word) const noexcept
    {
        if (variables.contains(std::string(word))) return true;
        if (case_insensitive)
        {
            std::string upper(word);
            std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            return variables.contains(upper);
        }
        return false;
    }
};

} // namespace Zenvra::Language::Syntax
