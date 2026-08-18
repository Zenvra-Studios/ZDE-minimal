#include "Language/Syntax/GenericGrammarEngine.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Language::Syntax
{

namespace
{

bool is_path_or_filename(std::string_view text) noexcept
{
    if (text.empty()) return false;
    if (text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos)
    {
        return true;
    }
    const std::size_t dot = text.rfind('.');
    if (dot != std::string_view::npos && dot > 0 && dot + 1 < text.size())
    {
        const std::string_view ext = text.substr(dot);
        return ext == ".mm" || ext == ".cpp" || ext == ".h" || ext == ".hpp" ||
               ext == ".c" || ext == ".cc" || ext == ".cxx" || ext == ".m" ||
               ext == ".rs" || ext == ".py" || ext == ".js" || ext == ".ts" ||
               ext == ".txt" || ext == ".cmake" || ext == ".json" || ext == ".xml" ||
               ext == ".html" || ext == ".css" || ext == ".in" || ext == ".rc" ||
               ext == ".def" || ext == ".lib" || ext == ".a" || ext == ".so" ||
               ext == ".dylib" || ext == ".dll" || ext == ".exe" || ext == ".o" ||
               ext == ".obj" || ext == ".ico" || ext == ".png" || ext == ".svg";
    }
    return false;
}

} // namespace

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

    enum class DeclContext
    {
        None,
        Namespace,
        Class,
        Function
    };
    DeclContext decl_context = DeclContext::None;

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

        // 5. Variable Expansions (${VAR}, $ENV{VAR}, $<...>, @VAR@)
        if (character == '$' && cursor + 1 < line.size())
        {
            const std::string_view remaining = line.substr(cursor);
            if (remaining.starts_with("${"))
            {
                const std::size_t close_pos = line.find('}', cursor + 2);
                if (close_pos != std::string_view::npos)
                {
                    cursor = close_pos + 1;
                    append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Label);
                    continue;
                }
            }
            else if (remaining.starts_with("$ENV{") || remaining.starts_with("$CACHE{"))
            {
                const std::size_t close_pos = line.find('}', cursor + 5);
                if (close_pos != std::string_view::npos)
                {
                    cursor = close_pos + 1;
                    append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Label);
                    continue;
                }
            }
            else if (remaining.starts_with("$<"))
            {
                const std::size_t close_pos = line.find('>', cursor + 2);
                if (close_pos != std::string_view::npos)
                {
                    cursor = close_pos + 1;
                    append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Label);
                    continue;
                }
            }
        }
        if (character == '@' && grammar.name == "CMake" && cursor + 1 < line.size())
        {
            const std::size_t next_at = line.find('@', cursor + 1);
            if (next_at != std::string_view::npos && next_at - cursor < 64 &&
                line.substr(cursor + 1, next_at - cursor - 1).find_first_of(" \t\r\n();{}") == std::string_view::npos)
            {
                cursor = next_at + 1;
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Label);
                continue;
            }
        }

        // 6. Strings
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

        // 7. Numbers (Decimal, Hex, Binary, Floats)
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

        // 8. Annotations / Decorators (e.g. @SpringBootApplication, @Component, @Injectable, @Override)
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

        // 9. File Paths & Source Files in Build Scripts (e.g. CocoaWindow.mm, Runtime/CocoaContext.mm)
        if (grammar.name == "CMake" || grammar.name == "Meson")
        {
            // Scan word boundaries up to whitespace / parentheses / quotes
            std::size_t word_end = cursor;
            while (word_end < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[word_end])) == 0 &&
                   line[word_end] != '(' && line[word_end] != ')' &&
                   line[word_end] != '"' && line[word_end] != '\'' &&
                   line[word_end] != '#' && line[word_end] != ';')
            {
                ++word_end;
            }
            const std::string_view candidate = line.substr(cursor, word_end - cursor);
            if (is_path_or_filename(candidate))
            {
                cursor = word_end;
                append(candidate, UI::Editor::EditorTokenKind::Plain);
                continue;
            }
        }

        // 10. Scoped Identifiers (e.g. Zenvra::Core, std::vector)
        if (std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_')
        {
            while (cursor < line.size())
            {
                if (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_')
                {
                    ++cursor;
                }
                else if (cursor + 1 < line.size() && line[cursor] == ':' && line[cursor + 1] == ':')
                {
                    cursor += 2;
                }
                else
                {
                    break;
                }
            }
            const std::string_view identifier = line.substr(token_start, cursor - token_start);

            // Lookahead after this identifier (ignoring whitespace)
            std::size_t next_idx = cursor;
            while (next_idx < line.size() && std::isspace(static_cast<unsigned char>(line[next_idx])) != 0)
            {
                ++next_idx;
            }

            const bool followed_by_paren = (next_idx < line.size() && line[next_idx] == '(');

            if (grammar.is_keyword(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Keyword);
                if (identifier == "namespace" || identifier == "package" || identifier == "import" ||
                    identifier == "mod" || identifier == "use")
                {
                    decl_context = DeclContext::Namespace;
                }
                else if (identifier == "class" || identifier == "struct" || identifier == "interface" ||
                         identifier == "enum" || identifier == "trait" || identifier == "record" || identifier == "union")
                {
                    decl_context = DeclContext::Class;
                }
                else if (identifier == "fn" || identifier == "def" || identifier == "function" || identifier == "func")
                {
                    decl_context = DeclContext::Function;
                }
            }
            else if (grammar.is_type(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Type);
                if (decl_context != DeclContext::Namespace)
                {
                    decl_context = DeclContext::None;
                }
            }
            else if (grammar.is_variable(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Label);
            }
            else if (followed_by_paren)
            {
                // In CMake and build files, any command/function call is a Keyword command
                if (grammar.name == "CMake" || grammar.name == "Meson")
                {
                    append(identifier, UI::Editor::EditorTokenKind::Keyword);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Label);
                }
            }
            else
            {
                bool is_label = false;

                if (decl_context == DeclContext::Namespace ||
                    decl_context == DeclContext::Class ||
                    decl_context == DeclContext::Function)
                {
                    is_label = true;
                    if (decl_context != DeclContext::Namespace)
                    {
                        decl_context = DeclContext::None;
                    }
                }
                else if (identifier.find("::") != std::string_view::npos)
                {
                    is_label = (grammar.name == "CMake");
                }
                else if (grammar.name != "CMake" && std::isupper(static_cast<unsigned char>(identifier.front())) != 0)
                {
                    is_label = true;
                }

                if (is_label)
                {
                    append(identifier, UI::Editor::EditorTokenKind::Label);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Plain);
                }
            }
            continue;
        }

        // 11. Operators and punctuation symbols
        const char punct = line[cursor];
        if (punct == ';' || punct == '{' || punct == '}' || punct == '(' || punct == ')')
        {
            decl_context = DeclContext::None;
        }
        else if (punct == ':' && cursor + 1 < line.size() && line[cursor + 1] == ':')
        {
            append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Plain);
            cursor += 2;
            continue;
        }
        ++cursor;
        append(line.substr(token_start, 1), UI::Editor::EditorTokenKind::Plain);
    }

    return token_count;
}

} // namespace Zenvra::Language::Syntax
