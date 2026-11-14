#pragma once

#include "Services/Language/Registry/LanguageRegistry.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Zenvra::Language
{

struct LanguageConfiguration
{
    std::size_t tab_size = 4;
    bool insert_spaces = true;
    bool auto_close_braces = true;
    bool auto_close_brackets = true;
    bool auto_close_parentheses = true;
    bool auto_close_quotes = true;
    bool auto_continue_comments = true;
    std::string line_comment = "//";
    std::string block_comment_start = "/*";
    std::string block_comment_end = "*/";

    static LanguageConfiguration get_for_extension(std::string_view extension)
    {
        const auto settings = Services::Language::Registry::LanguageRegistry::instance().get_editor_settings(extension);
        LanguageConfiguration config;
        config.tab_size = settings.tab_size;
        config.insert_spaces = settings.insert_spaces;
        config.auto_close_braces = settings.auto_close_braces;
        config.auto_close_brackets = settings.auto_close_brackets;
        config.auto_close_parentheses = settings.auto_close_parentheses;
        config.auto_close_quotes = settings.auto_close_quotes;
        config.auto_continue_comments = settings.auto_continue_comments;
        config.line_comment = settings.line_comment;
        config.block_comment_start = settings.block_comment_start;
        config.block_comment_end = settings.block_comment_end;
        return config;
    }

    static LanguageConfiguration get_for_file(std::string_view filename, std::string_view content = {})
    {
        const auto result = Services::Language::Registry::LanguageRegistry::instance().detect_language(filename, content);
        const auto settings = Services::Language::Registry::LanguageRegistry::instance().get_editor_settings(result.language_id);
        LanguageConfiguration config;
        config.tab_size = settings.tab_size;
        config.insert_spaces = settings.insert_spaces;
        config.auto_close_braces = settings.auto_close_braces;
        config.auto_close_brackets = settings.auto_close_brackets;
        config.auto_close_parentheses = settings.auto_close_parentheses;
        config.auto_close_quotes = settings.auto_close_quotes;
        config.auto_continue_comments = settings.auto_continue_comments;
        config.line_comment = settings.line_comment;
        config.block_comment_start = settings.block_comment_start;
        config.block_comment_end = settings.block_comment_end;
        return config;
    }
};

} // namespace Zenvra::Language
