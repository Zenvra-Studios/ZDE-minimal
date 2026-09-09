#pragma once

#include "UI/Theme/StudioTheme.h"

#include <filesystem>
#include <string>

namespace Zenvra::UI::Theme
{

struct ThemeInfo
{
    std::string id;
    std::string label;
    std::string plugin_id;
    std::filesystem::path file_path;
    bool is_builtin{false};
    StudioTheme theme_data;
};

} // namespace Zenvra::UI::Theme
