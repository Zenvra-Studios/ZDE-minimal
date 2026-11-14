#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Editor
{

/// Lightweight C++ scope scanner.
/// Scans the text lines backwards from the cursor to detect enclosing
/// namespace, class, struct, and function scopes.
class CppSymbolLexer
{
public:
    [[nodiscard]] static std::vector<BreadcrumbItem> resolve_scopes(
        std::span<const std::string> lines,
        std::size_t caret_line);
};

} // namespace Zenvra::UI::Editor
