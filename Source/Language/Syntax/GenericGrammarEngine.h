#pragma once

#include "Language/Syntax/GrammarRule.h"
#include "UI/Editor/StudioEditorModel.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace Zenvra::Language::Syntax
{

class GenericGrammarEngine
{
public:
    GenericGrammarEngine() = default;

    /// Tokenizes a line of source code based on dynamic grammar rules.
    /// Returns the number of tokens written into the output array.
    [[nodiscard]] static std::size_t tokenize_line(
        std::string_view line,
        const GrammarRule& grammar,
        std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>& output) noexcept;
};

} // namespace Zenvra::Language::Syntax
