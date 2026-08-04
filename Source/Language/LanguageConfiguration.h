#pragma once

#include <string>
#include <string_view>

namespace Zenvra::Language
{

struct LanguageConfiguration
{
    std::size_t tab_size = 4;
    bool insert_spaces = true;
    bool auto_close_braces = true;
    bool auto_continue_comments = true;
    std::string line_comment = "//";

    static LanguageConfiguration get_for_extension(std::string_view extension)
    {
        LanguageConfiguration config;
        config.tab_size = 4; // Default to 4
        
        if (extension == ".cpp" || extension == ".h" || extension == ".c" || extension == ".hpp")
        {
            config.auto_close_braces = true;
            config.line_comment = "//";
        }
        else if (extension == ".cmake" || extension == ".txt" || extension == ".py" || extension == ".sh" || extension == ".yml" || extension == ".yaml")
        {
            config.line_comment = "#";
        }
        
        return config;
    }
};

} // namespace Zenvra::Language
