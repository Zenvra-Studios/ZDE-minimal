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

    static LanguageConfiguration get_for_extension(std::string_view extension)
    {
        LanguageConfiguration config;
        config.tab_size = 4; // Default to 4
        
        if (extension == ".cpp" || extension == ".h" || extension == ".c" || extension == ".hpp")
        {
            config.auto_close_braces = true;
        }
        
        return config;
    }
};

} // namespace Zenvra::Language
