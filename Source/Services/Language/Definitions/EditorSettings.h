#pragma once

#include <cstddef>
#include <string>
#include <nlohmann/json.hpp>

namespace Zenvra::Services::Language::Definitions
{

struct EditorSettings
{
    std::size_t tab_size{4};
    bool insert_spaces{true};
    bool auto_close_braces{true};
    bool auto_close_brackets{true};
    bool auto_close_parentheses{true};
    bool auto_close_quotes{true};
    bool auto_continue_comments{true};
    std::string line_comment{"//"};
    std::string block_comment_start{"/*"};
    std::string block_comment_end{"*/"};
    std::string word_pattern{R"([a-zA-Z_][a-zA-Z0-9_]*)"};

    [[nodiscard]] nlohmann::json to_json() const;
    static EditorSettings from_json(const nlohmann::json& j);
};

} // namespace Zenvra::Services::Language::Definitions

