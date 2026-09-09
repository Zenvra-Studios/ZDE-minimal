#pragma once

#include <string_view>

namespace Zenvra::Editors
{

enum class VimMode
{
    Normal,
    Insert,
    Visual,
    VisualLine,
    Command,
    OperatorPending
};

[[nodiscard]] constexpr std::string_view vim_mode_to_string(VimMode mode) noexcept
{
    switch (mode)
    {
    case VimMode::Normal:          return "-- NORMAL --";
    case VimMode::Insert:          return "-- INSERT --";
    case VimMode::Visual:          return "-- VISUAL --";
    case VimMode::VisualLine:      return "-- VISUAL LINE --";
    case VimMode::Command:         return "-- COMMAND --";
    case VimMode::OperatorPending: return "-- OPERATOR PENDING --";
    }
    return "";
}

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using VimMode = Zenvra::Editors::VimMode;
using Zenvra::Editors::vim_mode_to_string;
} // namespace Zenvra::UI::Editor
