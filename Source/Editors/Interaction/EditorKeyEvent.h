#pragma once

#include <cstdint>
#include <string>

namespace Zenvra::Editors
{

struct EditorKeyEvent
{
    int key_code = 0;
    char32_t character = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    std::string text;
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using EditorKeyEvent = Zenvra::Editors::EditorKeyEvent;
} // namespace Zenvra::UI::Editor

