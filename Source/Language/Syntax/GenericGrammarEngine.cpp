#include "Language/Syntax/GenericGrammarEngine.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Language::Syntax
{

std::size_t GenericGrammarEngine::tokenize_line(
    std::string_view line,
    const GrammarRule& grammar,
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>& output) noexcept
{
    std::size_t token_count = 0;
    std::size_t cursor = 0;

    const auto append = [&output, &token_count](std::string_view text, UI::Editor::EditorTokenKind kind) {
        if (!text.empty() && token_count < output.size())
        {
            output[token_count++] = UI::Editor::EditorToken{text, kind};
        }
    };

    // Check for leading comment decorations like in JSDoc / Doxygen (* or /**)
    const std::size_t first_non_ws = line.find_first_not_of(" \t");
    if (first_non_ws != std::string_view::npos)
    {
        const std::string_view trimmed = line.substr(first_non_ws);
        if (trimmed.starts_with("/**") || trimmed.starts_with("/*") ||
            trimmed.starts_with("*/") || trimmed.starts_with("* ") ||
            trimmed == "*" || trimmed.starts_with("**/"))
        {
            if (!output.empty())
            {
                output[0] = UI::Editor::EditorToken{line, UI::Editor::EditorTokenKind::Comment};
                return 1;
            }
        }
    }

    while (cursor < line.size() && token_count < output.size())
    {
        const std::size_t token_start = cursor;
        const char character = line[cursor];

        // 1. Preprocessor (e.g. #include, #define in C/C++)
        if (grammar.supports_preprocessor && character == '#')
        {
            append(line.substr(cursor), UI::Editor::EditorTokenKind::Keyword);
            break;
        }

        // 2. Whitespace
        if (std::isspace(static_cast<unsigned char>(character)) != 0)
        {
            while (cursor < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
            {
                ++cursor;
            }
            append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Plain);
            continue;
        }

        // 3. Single-line comment
        if (!grammar.line_comment.empty() &&
            line.substr(cursor).starts_with(grammar.line_comment))
        {
            append(line.substr(cursor), UI::Editor::EditorTokenKind::Comment);
            break;
        }

        // 4. Block comment
        if (!grammar.block_comment_start.empty() &&
            line.substr(cursor).starts_with(grammar.block_comment_start))
        {
            cursor += grammar.block_comment_start.size();
            const std::size_t end_pos = line.find(grammar.block_comment_end, cursor);
            if (end_pos != std::string_view::npos)
            {
                cursor = end_pos + grammar.block_comment_end.size();
            }
            else
            {
                cursor = line.size();
            }
            append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Comment);
            continue;
        }

        // 5. Strings
        bool is_string_delim = false;
        for (const auto& delim : grammar.string_delimiters)
        {
            if (!delim.empty() && delim[0] == character)
            {
                is_string_delim = true;
                break;
            }
        }

        if (is_string_delim)
        {
            const char quote = character;
            ++cursor;
            while (cursor < line.size())
            {
                if (line[cursor] == '\\' && cursor + 1 < line.size())
                {
                    cursor += 2;
                    continue;
                }
                if (line[cursor] == quote)
                {
                    ++cursor;
                    break;
                }
                ++cursor;
            }
            append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
            continue;
        }

        // 6. Numbers (Decimal, Hex, Binary, Floats)
        if (std::isdigit(static_cast<unsigned char>(character)) != 0 ||
            (character == '-' && cursor + 1 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[cursor + 1])) != 0))
        {
            ++cursor;
            if (cursor < line.size() && (line[cursor] == 'x' || line[cursor] == 'X' || line[cursor] == 'b' || line[cursor] == 'B'))
            {
                ++cursor;
            }
            while (cursor < line.size() &&
                   (std::isxdigit(static_cast<unsigned char>(line[cursor])) != 0 ||
                    line[cursor] == '.' || line[cursor] == '_' || line[cursor] == 'f' || line[cursor] == 'u' || line[cursor] == 'l'))
            {
                ++cursor;
            }
            append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Number);
            continue;
        }

        // 7. Annotations / Decorators (e.g. @SpringBootApplication, @Component, @Injectable, @Override)
        if (character == '@' && cursor + 1 < line.size() &&
            (std::isalpha(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_'))
        {
            ++cursor;
            while (cursor < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 ||
                    line[cursor] == '_'))
            {
                ++cursor;
            }
            append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Keyword);
            continue;
        }

        // 8. Identifiers (Keywords, Types, Plain)
        if (std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_')
        {
            while (cursor < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 ||
                    line[cursor] == '_'))
            {
                ++cursor;
            }
            const std::string_view identifier = line.substr(token_start, cursor - token_start);
            if (grammar.is_keyword(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Keyword);
            }
            else if (grammar.is_type(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Type);
            }
            else
            {
                append(identifier, UI::Editor::EditorTokenKind::Plain);
            }
            continue;
        }

        // 9. Operators and punctuation symbols
        ++cursor;
        append(line.substr(token_start, 1), UI::Editor::EditorTokenKind::Plain);
    }

    return token_count;
}

} // namespace Zenvra::Language::Syntax
