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
               ext == ".asm" || ext == ".s" || ext == ".S" || ext == ".nasm" || ext == ".inc" || ext == ".a51" ||
               ext == ".txt" || ext == ".cmake" || ext == ".json" || ext == ".xml" ||
               ext == ".html" || ext == ".css" || ext == ".in" || ext == ".rc" ||
               ext == ".def" || ext == ".lib" || ext == ".a" || ext == ".so" ||
               ext == ".dylib" || ext == ".dll" || ext == ".exe" || ext == ".o" ||
               ext == ".obj" || ext == ".ico" || ext == ".png" || ext == ".svg";
    }
    return false;
}

bool is_all_caps_constant(std::string_view word) noexcept
{
    if (word.size() < 2) return false;
    bool has_letter = false;
    for (char c : word)
    {
        if (std::islower(static_cast<unsigned char>(c)) != 0) return false;
        if (std::isupper(static_cast<unsigned char>(c)) != 0) has_letter = true;
    }
    return has_letter;
}

bool is_pascal_case_type(std::string_view word) noexcept
{
    if (word.size() < 2) return false;
    if (std::isupper(static_cast<unsigned char>(word[0])) == 0) return false;

    // True PascalCase types never contain underscores (e.g. Assets_icons_... is snake_case)
    if (word.find('_') != std::string_view::npos) return false;

    // Must contain at least one lowercase letter to avoid all-caps constants
    bool has_lower = false;
    for (std::size_t i = 1; i < word.size(); ++i)
    {
        if (std::islower(static_cast<unsigned char>(word[i])) != 0)
        {
            has_lower = true;
            break;
        }
    }
    return has_lower;
}

} // namespace

std::size_t GenericGrammarEngine::tokenize_line(
    std::string_view line,
    const GrammarRule& grammar,
    std::array<UI::Editor::EditorToken, UI::Editor::maximum_editor_tokens>& output,
    TokenizerState& state) noexcept
{
    std::size_t token_count = 0;
    std::size_t cursor = 0;

    const auto append = [&output, &token_count](std::string_view text, UI::Editor::EditorTokenKind kind) {
        if (!text.empty() && token_count < output.size())
        {
            output[token_count++] = UI::Editor::EditorToken{text, kind};
        }
    };

    // Handle resumed state from previous lines
    if (state.kind == TokenizerState::StateKind::BlockComment)
    {
        const std::string_view end_token = !state.custom_delimiter.empty()
            ? std::string_view(state.custom_delimiter)
            : (!grammar.block_comment_end.empty()
                ? std::string_view(grammar.block_comment_end)
                : std::string_view("*/"));
        const std::size_t end_pos = line.find(end_token);
        if (end_pos != std::string_view::npos)
        {
            cursor = end_pos + end_token.size();
            append(line.substr(0, cursor), UI::Editor::EditorTokenKind::Comment);
            state = TokenizerState{};
        }
        else
        {
            append(line, UI::Editor::EditorTokenKind::Comment);
            return token_count;
        }
    }
    else if (state.kind == TokenizerState::StateKind::RawString)
    {
        const std::string closing_pattern = ")" + state.custom_delimiter + "\"";
        const std::size_t end_pos = line.find(closing_pattern);
        if (end_pos != std::string_view::npos)
        {
            cursor = end_pos + closing_pattern.size();
            append(line.substr(0, cursor), UI::Editor::EditorTokenKind::String);
            state = TokenizerState{};
        }
        else
        {
            append(line, UI::Editor::EditorTokenKind::String);
            return token_count;
        }
    }
    else if (state.kind == TokenizerState::StateKind::TripleQuoteString)
    {
        const std::string closing_pattern = std::string(3, state.quote_char != 0 ? state.quote_char : '"');
        const std::size_t end_pos = line.find(closing_pattern);
        if (end_pos != std::string_view::npos)
        {
            cursor = end_pos + 3;
            append(line.substr(0, cursor), UI::Editor::EditorTokenKind::String);
            state = TokenizerState{};
        }
        else
        {
            append(line, UI::Editor::EditorTokenKind::String);
            return token_count;
        }
    }
    else if (state.kind == TokenizerState::StateKind::MultilineString)
    {
        const char quote = state.quote_char != 0 ? state.quote_char : '`';
        std::size_t scan = 0;
        bool closed = false;
        while (scan < line.size())
        {
            if (line[scan] == '\\' && scan + 1 < line.size())
            {
                scan += 2;
                continue;
            }
            if (line[scan] == quote)
            {
                closed = true;
                cursor = scan + 1;
                append(line.substr(0, cursor), UI::Editor::EditorTokenKind::String);
                state = TokenizerState{};
                break;
            }
            ++scan;
        }
        if (!closed)
        {
            append(line, UI::Editor::EditorTokenKind::String);
            return token_count;
        }
    }
    else if (state.kind == TokenizerState::StateKind::BackslashString)
    {
        const char quote = state.quote_char != 0 ? state.quote_char : '"';
        std::size_t scan = 0;
        bool closed = false;
        while (scan < line.size())
        {
            if (line[scan] == '\\' && scan + 1 < line.size())
            {
                scan += 2;
                continue;
            }
            if (line[scan] == quote)
            {
                closed = true;
                cursor = scan + 1;
                append(line.substr(0, cursor), UI::Editor::EditorTokenKind::String);
                state = TokenizerState{};
                break;
            }
            ++scan;
        }
        if (!closed)
        {
            append(line, UI::Editor::EditorTokenKind::String);
            if (!line.ends_with('\\'))
            {
                state = TokenizerState{};
            }
            return token_count;
        }
    }
    else if (state.kind == TokenizerState::StateKind::CppAttribute)
    {
        bool closed = false;
        while (cursor < line.size() && token_count < output.size())
        {
            if (line[cursor] == ']' && cursor + 1 < line.size() && line[cursor + 1] == ']')
            {
                append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
                cursor += 2;
                closed = true;
                state = TokenizerState{};
                break;
            }
            if (std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
            {
                std::size_t space_start = cursor;
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    ++cursor;
                }
                append(line.substr(space_start, cursor - space_start), UI::Editor::EditorTokenKind::Plain);
                continue;
            }
            if (line[cursor] == '"')
            {
                std::size_t str_start = cursor;
                ++cursor;
                while (cursor < line.size() && line[cursor] != '"')
                {
                    if (line[cursor] == '\\' && cursor + 1 < line.size())
                    {
                        cursor += 2;
                    }
                    else
                    {
                        ++cursor;
                    }
                }
                if (cursor < line.size() && line[cursor] == '"')
                {
                    ++cursor;
                }
                append(line.substr(str_start, cursor - str_start), UI::Editor::EditorTokenKind::String);
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(line[cursor])) != 0)
            {
                std::size_t num_start = cursor;
                while (cursor < line.size() && std::isalnum(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    ++cursor;
                }
                append(line.substr(num_start, cursor - num_start), UI::Editor::EditorTokenKind::Number);
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_')
            {
                std::size_t id_start = cursor;
                while (cursor < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                {
                    ++cursor;
                }
                append(line.substr(id_start, cursor - id_start), UI::Editor::EditorTokenKind::Directive);
                continue;
            }
            if (line[cursor] == ':' && cursor + 1 < line.size() && line[cursor + 1] == ':')
            {
                append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Plain);
                cursor += 2;
                continue;
            }
            append(line.substr(cursor, 1), UI::Editor::EditorTokenKind::Plain);
            ++cursor;
        }
        if (!closed)
        {
            return token_count;
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

        // 0. PHP Open & Close Tags, Blade Directives (@extends, @section, etc.), and PHP 8 Attributes (#[Attribute])
        if (grammar.name == "PHP" || grammar.name == "HTML" || grammar.name == "HTML/CSS")
        {
            if (character == '<' && cursor + 1 < line.size() && line[cursor + 1] == '?')
            {
                if (line.substr(cursor).starts_with("<?php"))
                {
                    append(line.substr(cursor, 5), UI::Editor::EditorTokenKind::Directive);
                    cursor += 5;
                    continue;
                }
                else if (line.substr(cursor).starts_with("<?="))
                {
                    append(line.substr(cursor, 3), UI::Editor::EditorTokenKind::Directive);
                    cursor += 3;
                    continue;
                }
                else if (line.substr(cursor).starts_with("<?"))
                {
                    append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
                    cursor += 2;
                    continue;
                }
            }
            else if (character == '?' && cursor + 1 < line.size() && line[cursor + 1] == '>')
            {
                append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
                cursor += 2;
                continue;
            }
            else if (character == '#' && cursor + 1 < line.size() && line[cursor + 1] == '[')
            {
                append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
                cursor += 2;
                continue;
            }
            else if (character == '@' && cursor + 1 < line.size() &&
                     (std::isalpha(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_'))
            {
                std::size_t dir_end = cursor + 1;
                while (dir_end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[dir_end])) != 0 || line[dir_end] == '_'))
                {
                    ++dir_end;
                }
                std::size_t peek = dir_end;
                while (peek < line.size() && std::isspace(static_cast<unsigned char>(line[peek])) != 0)
                {
                    ++peek;
                }
                const bool is_event_attr = (peek < line.size() && line[peek] == '=');
                append(line.substr(cursor, dir_end - cursor),
                       is_event_attr ? UI::Editor::EditorTokenKind::Label : UI::Editor::EditorTokenKind::Keyword);
                cursor = dir_end;
                continue;
            }
        }

        // CSS Hex Colors (e.g. #fff, #ffffff, #2a2b2e) or CSS ID selectors in CSS / HTML
        if ((grammar.name == "CSS" || grammar.name == "HTML" || grammar.name == "HTML/CSS") && character == '#' && cursor + 1 < line.size() &&
            (std::isalnum(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_' || line[cursor + 1] == '-'))
        {
            std::size_t hex_end = cursor + 1;
            bool all_hex = true;
            while (hex_end < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[hex_end])) != 0 || line[hex_end] == '_' || line[hex_end] == '-'))
            {
                if (std::isxdigit(static_cast<unsigned char>(line[hex_end])) == 0)
                {
                    all_hex = false;
                }
                ++hex_end;
            }
            const std::size_t hex_len = hex_end - (cursor + 1);
            if (all_hex && (hex_len == 3 || hex_len == 4 || hex_len == 6 || hex_len == 8))
            {
                append(line.substr(cursor, hex_end - cursor), UI::Editor::EditorTokenKind::Number);
            }
            else
            {
                append(line.substr(cursor, hex_end - cursor), UI::Editor::EditorTokenKind::Label);
            }
            cursor = hex_end;
            continue;
        }

        // CSS Custom Properties / Variables (e.g. --primary-color: #fff, var(--my-var))
        if ((grammar.name == "CSS" || grammar.name == "HTML" || grammar.name == "HTML/CSS" || grammar.name == "PHP") &&
            character == '-' && cursor + 1 < line.size() && line[cursor + 1] == '-' &&
            cursor + 2 < line.size() && (std::isalpha(static_cast<unsigned char>(line[cursor + 2])) != 0 || line[cursor + 2] == '_'))
        {
            std::size_t var_end = cursor + 2;
            while (var_end < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[var_end])) != 0 || line[var_end] == '_' || line[var_end] == '-'))
            {
                ++var_end;
            }
            append(line.substr(cursor, var_end - cursor), UI::Editor::EditorTokenKind::Macro);
            cursor = var_end;
            continue;
        }

        // 0c. C++ Attributes ([[nodiscard]], [[maybe_unused]], [[deprecated]], [[fallthrough]], etc.)
        if ((grammar.name == "C/C++" || grammar.supports_preprocessor) &&
            character == '[' && cursor + 1 < line.size() && line[cursor + 1] == '[')
        {
            append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
            cursor += 2;
            bool closed = false;
            while (cursor < line.size() && token_count < output.size())
            {
                if (line[cursor] == ']' && cursor + 1 < line.size() && line[cursor + 1] == ']')
                {
                    append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Directive);
                    cursor += 2;
                    closed = true;
                    break;
                }
                if (std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    std::size_t space_start = cursor;
                    while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                    {
                        ++cursor;
                    }
                    append(line.substr(space_start, cursor - space_start), UI::Editor::EditorTokenKind::Plain);
                    continue;
                }
                if (line[cursor] == '"')
                {
                    std::size_t str_start = cursor;
                    ++cursor;
                    while (cursor < line.size() && line[cursor] != '"')
                    {
                        if (line[cursor] == '\\' && cursor + 1 < line.size())
                        {
                            cursor += 2;
                        }
                        else
                        {
                            ++cursor;
                        }
                    }
                    if (cursor < line.size() && line[cursor] == '"')
                    {
                        ++cursor;
                    }
                    append(line.substr(str_start, cursor - str_start), UI::Editor::EditorTokenKind::String);
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    std::size_t num_start = cursor;
                    while (cursor < line.size() && std::isalnum(static_cast<unsigned char>(line[cursor])) != 0)
                    {
                        ++cursor;
                    }
                    append(line.substr(num_start, cursor - num_start), UI::Editor::EditorTokenKind::Number);
                    continue;
                }
                if (std::isalpha(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_')
                {
                    std::size_t id_start = cursor;
                    while (cursor < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                    {
                        ++cursor;
                    }
                    append(line.substr(id_start, cursor - id_start), UI::Editor::EditorTokenKind::Directive);
                    continue;
                }
                if (line[cursor] == ':' && cursor + 1 < line.size() && line[cursor + 1] == ':')
                {
                    append(line.substr(cursor, 2), UI::Editor::EditorTokenKind::Plain);
                    cursor += 2;
                    continue;
                }
                append(line.substr(cursor, 1), UI::Editor::EditorTokenKind::Plain);
                ++cursor;
            }
            if (!closed)
            {
                state.kind = TokenizerState::StateKind::CppAttribute;
                return token_count;
            }
            continue;
        }

        // 1. Preprocessor (e.g. #include, #define, #pragma, #ifdef in C/C++, C#, ASM)
        const bool is_hash_directive = (grammar.supports_preprocessor && character == '#') ||
                                       (grammar.name == "Rust" && character == '#');
        const bool is_nasm_directive = (grammar.name == "Assembly" && character == '%');

        if (is_hash_directive || is_nasm_directive)
        {
            if (grammar.name == "Rust")
            {
                std::size_t attr_end = cursor + 1;
                if (attr_end < line.size() && line[attr_end] == '!') ++attr_end;
                if (attr_end < line.size() && line[attr_end] == '[')
                {
                    append(line.substr(cursor, attr_end + 1 - cursor), UI::Editor::EditorTokenKind::Directive);
                    cursor = attr_end + 1;
                    continue;
                }
            }

            std::size_t dir_end = cursor + 1;
            while (dir_end < line.size() && std::isspace(static_cast<unsigned char>(line[dir_end])) != 0)
            {
                ++dir_end;
            }
            std::size_t dir_name_start = dir_end;
            while (dir_end < line.size() && (std::isalnum(static_cast<unsigned char>(line[dir_end])) != 0 || line[dir_end] == '_'))
            {
                ++dir_end;
            }
            std::string_view directive = line.substr(dir_name_start, dir_end - dir_name_start);
            append(line.substr(cursor, dir_end - cursor), UI::Editor::EditorTokenKind::Directive);
            cursor = dir_end;

            if (directive == "include")
            {
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    append(line.substr(cursor, 1), UI::Editor::EditorTokenKind::Plain);
                    ++cursor;
                }
                if (cursor < line.size())
                {
                    const char open_c = line[cursor];
                    if (open_c == '<' || open_c == '"')
                    {
                        const char close_c = (open_c == '<') ? '>' : '"';
                        std::size_t path_end = cursor + 1;
                        while (path_end < line.size() && line[path_end] != close_c)
                        {
                            ++path_end;
                        }
                        if (path_end < line.size() && line[path_end] == close_c)
                        {
                            ++path_end;
                        }
                        append(line.substr(cursor, path_end - cursor), UI::Editor::EditorTokenKind::IncludeHeader);
                        cursor = path_end;
                    }
                }
            }
            else if (directive == "define" || directive == "macro")
            {
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    append(line.substr(cursor, 1), UI::Editor::EditorTokenKind::Plain);
                    ++cursor;
                }
                if (cursor < line.size() && (std::isalpha(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                {
                    std::size_t macro_start = cursor;
                    while (cursor < line.size() && (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                    {
                        ++cursor;
                    }
                    append(line.substr(macro_start, cursor - macro_start), UI::Editor::EditorTokenKind::Macro);
                }
            }
            else if (directive == "ifdef" || directive == "ifndef" || directive == "undef")
            {
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    append(line.substr(cursor, 1), UI::Editor::EditorTokenKind::Plain);
                    ++cursor;
                }
                if (cursor < line.size() && (std::isalpha(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                {
                    std::size_t macro_start = cursor;
                    while (cursor < line.size() && (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                    {
                        ++cursor;
                    }
                    append(line.substr(macro_start, cursor - macro_start), UI::Editor::EditorTokenKind::Macro);
                }
            }
            else if (directive == "if" || directive == "elif")
            {
                while (cursor < line.size())
                {
                    if (line.substr(cursor).starts_with("//"))
                    {
                        append(line.substr(cursor), UI::Editor::EditorTokenKind::Comment);
                        cursor = line.size();
                        break;
                    }
                    if (line.substr(cursor).starts_with("/*"))
                    {
                        break;
                    }

                    if (std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                    {
                        std::size_t ws_start = cursor;
                        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])) != 0)
                        {
                            ++cursor;
                        }
                        append(line.substr(ws_start, cursor - ws_start), UI::Editor::EditorTokenKind::Plain);
                        continue;
                    }

                    if (std::isalpha(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_')
                    {
                        std::size_t id_start = cursor;
                        while (cursor < line.size() && (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
                        {
                            ++cursor;
                        }
                        std::string_view id = line.substr(id_start, cursor - id_start);
                        if (id == "defined")
                        {
                            append(id, UI::Editor::EditorTokenKind::Directive);
                        }
                        else
                        {
                            append(id, UI::Editor::EditorTokenKind::Macro);
                        }
                        continue;
                    }

                    if (std::isdigit(static_cast<unsigned char>(line[cursor])) != 0)
                    {
                        std::size_t num_start = cursor;
                        while (cursor < line.size() && (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '.'))
                        {
                            ++cursor;
                        }
                        append(line.substr(num_start, cursor - num_start), UI::Editor::EditorTokenKind::Number);
                        continue;
                    }

                    std::size_t op_start = cursor;
                    if (cursor + 1 < line.size() &&
                        ((line[cursor] == '&' && line[cursor + 1] == '&') ||
                         (line[cursor] == '|' && line[cursor + 1] == '|') ||
                         (line[cursor] == '=' && line[cursor + 1] == '=') ||
                         (line[cursor] == '!' && line[cursor + 1] == '=')))
                    {
                        cursor += 2;
                    }
                    else
                    {
                        ++cursor;
                    }
                    append(line.substr(op_start, cursor - op_start), UI::Editor::EditorTokenKind::Plain);
                }
            }
            continue;
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
        if ((!grammar.line_comment.empty() &&
             line.substr(cursor).starts_with(grammar.line_comment)) ||
            (grammar.name == "Assembly" && (character == ';' || character == '@' || line.substr(cursor).starts_with("//"))) ||
            (grammar.name == "PHP" && character == '#'))
        {
            append(line.substr(cursor), UI::Editor::EditorTokenKind::Comment);
            break;
        }

        // 4. Block comment
        if (line.substr(cursor).starts_with("<!--") &&
            (grammar.name == "HTML" || grammar.name == "HTML/CSS" || grammar.name == "PHP" || grammar.name == "Vue" ||
             grammar.name == "Svelte" || grammar.name == "XML"))
        {
            cursor += 4;
            const std::size_t end_pos = line.find("-->", cursor);
            if (end_pos != std::string_view::npos)
            {
                cursor = end_pos + 3;
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Comment);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::Comment);
                state.kind = TokenizerState::StateKind::BlockComment;
                state.custom_delimiter = "-->";
                return token_count;
            }
        }

        if (line.substr(cursor).starts_with("{{--") && grammar.name == "PHP")
        {
            cursor += 4;
            const std::size_t end_pos = line.find("--}}", cursor);
            if (end_pos != std::string_view::npos)
            {
                cursor = end_pos + 4;
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Comment);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::Comment);
                state.kind = TokenizerState::StateKind::BlockComment;
                state.custom_delimiter = "--}}";
                return token_count;
            }
        }

        if (!grammar.block_comment_start.empty() &&
            line.substr(cursor).starts_with(grammar.block_comment_start))
        {
            cursor += grammar.block_comment_start.size();
            const std::size_t end_pos = line.find(grammar.block_comment_end, cursor);
            if (end_pos != std::string_view::npos)
            {
                cursor = end_pos + grammar.block_comment_end.size();
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Comment);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::Comment);
                state.kind = TokenizerState::StateKind::BlockComment;
                state.custom_delimiter = grammar.block_comment_end;
                return token_count;
            }
        }

        // 5. Variable Expansions (${VAR}, $ENV{VAR}, $<...>, PHP $variables)
        if (character == '$' && cursor + 1 < line.size())
        {
            if (grammar.name == "PHP" &&
                (std::isalpha(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_'))
            {
                std::size_t var_end = cursor + 1;
                while (var_end < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[var_end])) != 0 || line[var_end] == '_'))
                {
                    ++var_end;
                }
                const std::string_view var_name = line.substr(cursor, var_end - cursor);
                cursor = var_end;
                if (var_name == "$this")
                {
                    append(var_name, UI::Editor::EditorTokenKind::Keyword);
                }
                else
                {
                    append(var_name, UI::Editor::EditorTokenKind::Macro);
                }
                continue;
            }

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

        // 6. Strings & Raw Strings
        // 6a. C/C++ Raw String Literal: R"delim(...)delim", u8R"delim(...)delim", LR"...", etc.
        bool is_cpp_raw_string = false;
        std::size_t raw_prefix_len = 0;
        if (grammar.name == "C/C++" || grammar.supports_preprocessor)
        {
            if (character == 'R' && cursor + 1 < line.size() && line[cursor + 1] == '"')
            {
                is_cpp_raw_string = true;
                raw_prefix_len = 1;
            }
            else if (cursor + 3 < line.size() && line.substr(cursor, 3) == "u8R" && line[cursor + 3] == '"')
            {
                is_cpp_raw_string = true;
                raw_prefix_len = 3;
            }
            else if (cursor + 2 < line.size() && (character == 'L' || character == 'u' || character == 'U') && line[cursor + 1] == 'R' && line[cursor + 2] == '"')
            {
                is_cpp_raw_string = true;
                raw_prefix_len = 2;
            }
        }

        if (is_cpp_raw_string)
        {
            const std::size_t quote_pos = cursor + raw_prefix_len; // Points to '"'
            const std::size_t open_paren = line.find('(', quote_pos + 1);
            if (open_paren != std::string_view::npos && (open_paren - (quote_pos + 1) <= 16))
            {
                const std::string delim = std::string(line.substr(quote_pos + 1, open_paren - (quote_pos + 1)));
                bool valid_delim = true;
                for (char c : delim)
                {
                    if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ')' || c == '\\')
                    {
                        valid_delim = false;
                        break;
                    }
                }
                if (valid_delim)
                {
                    const std::string closing_pattern = ")" + delim + "\"";
                    const std::size_t close_pos = line.find(closing_pattern, open_paren + 1);
                    if (close_pos != std::string_view::npos)
                    {
                        cursor = close_pos + closing_pattern.size();
                        append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
                        continue;
                    }
                    else
                    {
                        append(line.substr(token_start), UI::Editor::EditorTokenKind::String);
                        state.kind = TokenizerState::StateKind::RawString;
                        state.custom_delimiter = delim;
                        return token_count;
                    }
                }
            }
        }

        // 6b. Rust Raw String Literal: r"...", r#"..."#, r##"..."##
        if (grammar.name == "Rust" && character == 'r' && cursor + 1 < line.size())
        {
            std::size_t hashes = 0;
            std::size_t scan = cursor + 1;
            while (scan < line.size() && line[scan] == '#')
            {
                ++hashes;
                ++scan;
            }
            if (scan < line.size() && line[scan] == '"')
            {
                const std::string closing_pattern = "\"" + std::string(hashes, '#');
                const std::size_t close_pos = line.find(closing_pattern, scan + 1);
                if (close_pos != std::string_view::npos)
                {
                    cursor = close_pos + closing_pattern.size();
                    append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
                    continue;
                }
                else
                {
                    append(line.substr(token_start), UI::Editor::EditorTokenKind::String);
                    state.kind = TokenizerState::StateKind::RawString;
                    state.custom_delimiter = std::string(hashes, '#');
                    return token_count;
                }
            }
        }

        // 6c. Triple-quoted Strings (""" or ''')
        if (line.substr(cursor).starts_with("\"\"\"") || line.substr(cursor).starts_with("'''"))
        {
            const std::string_view tquote = line.substr(cursor, 3);
            const std::size_t close_pos = line.find(tquote, cursor + 3);
            if (close_pos != std::string_view::npos)
            {
                cursor = close_pos + 3;
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::String);
                state.kind = TokenizerState::StateKind::TripleQuoteString;
                state.quote_char = tquote[0];
                return token_count;
            }
        }

        // 6d. Backticks / Template Literals (`)
        if (character == '`')
        {
            std::size_t scan = cursor + 1;
            bool closed = false;
            while (scan < line.size())
            {
                if (line[scan] == '\\' && scan + 1 < line.size())
                {
                    scan += 2;
                    continue;
                }
                if (line[scan] == '`')
                {
                    closed = true;
                    cursor = scan + 1;
                    append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
                    break;
                }
                ++scan;
            }
            if (closed)
            {
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::String);
                state.kind = TokenizerState::StateKind::MultilineString;
                state.quote_char = '`';
                return token_count;
            }
        }

        // 6e. Standard String Delimiters (" and ')
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
            bool closed = false;
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
                    closed = true;
                    break;
                }
                ++cursor;
            }
            if (closed)
            {
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::String);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::String);
                if (line.ends_with('\\'))
                {
                    state.kind = TokenizerState::StateKind::BackslashString;
                    state.quote_char = quote;
                }
                return token_count;
            }
        }

        // 7. Numbers (Decimal, Hex, Binary, Floats, and CSS dimension units like 16px, 50%, 1.5rem)
        if (std::isdigit(static_cast<unsigned char>(character)) != 0 ||
            (character == '-' && cursor + 1 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[cursor + 1])) != 0))
        {
            ++cursor;
            if (cursor < line.size() && (line[cursor] == 'x' || line[cursor] == 'X' || line[cursor] == 'b' || line[cursor] == 'B'))
            {
                ++cursor;
            }
            while (cursor < line.size())
            {
                const char c = line[cursor];
                if (std::isxdigit(static_cast<unsigned char>(c)) != 0 ||
                    c == '.' || c == '_' || c == 'f' || c == 'u' || c == 'l' || c == 'F' || c == 'U' || c == 'L')
                {
                    ++cursor;
                }
                else if (c == '\'' && cursor + 1 < line.size() &&
                         std::isxdigit(static_cast<unsigned char>(line[cursor + 1])) != 0)
                {
                    cursor += 2;
                }
                else
                {
                    break;
                }
            }
            // If immediately followed by a CSS unit (% or px, rem, em, vh, vw, pt, deg, s, ms, etc.) in CSS / HTML / PHP
            if (grammar.name == "CSS" || grammar.name == "HTML" || grammar.name == "HTML/CSS" || grammar.name == "PHP" ||
                grammar.name == "Vue" || grammar.name == "Svelte")
            {
                if (cursor < line.size() && line[cursor] == '%')
                {
                    ++cursor;
                }
                else if (cursor < line.size() && std::isalpha(static_cast<unsigned char>(line[cursor])) != 0)
                {
                    std::size_t unit_end = cursor;
                    while (unit_end < line.size() && std::isalpha(static_cast<unsigned char>(line[unit_end])) != 0)
                    {
                        ++unit_end;
                    }
                    const std::string_view unit = line.substr(cursor, unit_end - cursor);
                    if (unit == "px" || unit == "rem" || unit == "em" || unit == "vh" || unit == "vw" ||
                        unit == "vmin" || unit == "vmax" || unit == "pt" || unit == "pc" || unit == "in" ||
                        unit == "cm" || unit == "mm" || unit == "deg" || unit == "rad" || unit == "turn" ||
                        unit == "s" || unit == "ms" || unit == "fr" || unit == "ch" || unit == "ex" ||
                        unit == "dvh" || unit == "dvw" || unit == "cqw" || unit == "cqh")
                    {
                        cursor = unit_end;
                    }
                }
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

        // 10. Identifiers (e.g. Zenvra, EditorScrollbar, reset, std)
        if (std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_')
        {
            // Lookbehind before this identifier (ignoring whitespace)
            std::size_t prev_idx = token_start;
            while (prev_idx > 0 && std::isspace(static_cast<unsigned char>(line[prev_idx - 1])) != 0)
            {
                --prev_idx;
            }
            const bool preceded_by_tag_open = (prev_idx > 0 && line[prev_idx - 1] == '<');
            const bool preceded_by_tag_close = (prev_idx >= 2 && line[prev_idx - 2] == '<' && line[prev_idx - 1] == '/');
            const bool preceded_by_doctype = (prev_idx >= 2 && line[prev_idx - 2] == '<' && line[prev_idx - 1] == '!');
            const bool preceded_by_dot = (prev_idx > 0 && line[prev_idx - 1] == '.');
            const bool preceded_by_arrow = (prev_idx >= 2 && line[prev_idx - 2] == '-' && line[prev_idx - 1] == '>');
            const bool is_jsx_or_html = (grammar.name == "HTML" || grammar.name == "HTML/CSS" ||
                                         grammar.name == "JavaScript/TypeScript" ||
                                         grammar.name == "Vue" || grammar.name == "Svelte" ||
                                         grammar.name == "PHP" || grammar.name == "CSS");

            // Lookbehind to previous non-whitespace token emitted on this line
            UI::Editor::EditorTokenKind prev_token_kind = UI::Editor::EditorTokenKind::Plain;
            std::string_view prev_token_text;
            if (token_count > 0)
            {
                std::size_t p = token_count;
                while (p > 0)
                {
                    --p;
                    bool all_space = true;
                    for (char ch : output[p].text)
                    {
                        if (std::isspace(static_cast<unsigned char>(ch)) == 0)
                        {
                            all_space = false;
                            break;
                        }
                    }
                    if (!all_space)
                    {
                        prev_token_kind = output[p].kind;
                        prev_token_text = output[p].text;
                        break;
                    }
                }
            }

            if (is_jsx_or_html && (preceded_by_tag_open || preceded_by_tag_close))
            {
                while (cursor < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 ||
                        line[cursor] == '_' || line[cursor] == '-' || line[cursor] == ':'))
                {
                    ++cursor;
                }
            }
            else if (is_jsx_or_html)
            {
                const bool is_css_or_markup = (grammar.name == "CSS" || grammar.name == "HTML" ||
                                               grammar.name == "HTML/CSS" || grammar.name == "Vue" ||
                                               grammar.name == "Svelte" || grammar.name == "PHP");
                while (cursor < line.size())
                {
                    if (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_')
                    {
                        ++cursor;
                    }
                    else if (is_css_or_markup && line[cursor] == '-' && cursor + 1 < line.size() &&
                             (std::isalnum(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_'))
                    {
                        ++cursor;
                    }
                    else if (line[cursor] == ':' && cursor + 1 < line.size() &&
                             (std::isalnum(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '_'))
                    {
                        // In XML/JSX/Vue directives like xmlns:xlink or v-bind:prop="val"
                        std::size_t peek = cursor + 1;
                        while (peek < line.size() &&
                               (std::isalnum(static_cast<unsigned char>(line[peek])) != 0 ||
                                line[peek] == '_' || line[peek] == '-'))
                        {
                            ++peek;
                        }
                        while (peek < line.size() && std::isspace(static_cast<unsigned char>(line[peek])) != 0)
                        {
                            ++peek;
                        }
                        if (peek < line.size() && line[peek] == '=')
                        {
                            ++cursor;
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else
            {
                while (cursor < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_' ||
                        (is_jsx_or_html && line[cursor] == '-' && cursor + 1 < line.size() &&
                         (std::isalpha(static_cast<unsigned char>(line[cursor + 1])) != 0 || line[cursor + 1] == '-'))))
                {
                    ++cursor;
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
            const bool followed_by_scope = (next_idx + 1 < line.size() && line[next_idx] == ':' && line[next_idx + 1] == ':');
            const bool followed_by_equals = (next_idx < line.size() && line[next_idx] == '=');
            const bool followed_by_colon = (next_idx < line.size() && line[next_idx] == ':' && !followed_by_scope);
            const bool followed_by_curly = (next_idx < line.size() && line[next_idx] == '{');
            const bool followed_by_comma = (next_idx < line.size() && line[next_idx] == ',');

            if (is_jsx_or_html && (preceded_by_tag_open || preceded_by_tag_close || preceded_by_doctype))
            {
                // JSX / HTML Tag name (e.g. <div, <Button, </span, </Modal, <!DOCTYPE)
                if (is_pascal_case_type(identifier))
                {
                    append(identifier, UI::Editor::EditorTokenKind::Type);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Keyword);
                }
            }
            else if (is_jsx_or_html && followed_by_equals)
            {
                // JSX / HTML Attribute name (e.g. className=, onClick=, id=, src=)
                append(identifier, UI::Editor::EditorTokenKind::Label);
            }
            else if (is_jsx_or_html && followed_by_colon &&
                     identifier != "case" && identifier != "default" && identifier != "public" &&
                     identifier != "private" && identifier != "protected")
            {
                // CSS Property name or JS/JSON object key (e.g. border-collapse:, width:, text-align:, padding:)
                append(identifier, UI::Editor::EditorTokenKind::Label);
            }
            else if (is_jsx_or_html && preceded_by_dot && (followed_by_curly || followed_by_comma || grammar.name == "CSS" || grammar.name == "HTML/CSS"))
            {
                // CSS Class selector (e.g. .logo {, .btn,)
                append(identifier, UI::Editor::EditorTokenKind::Type);
            }
            else if (identifier == "__attribute__" || identifier == "__attribute" || identifier == "__declspec" ||
                     identifier == "UPROPERTY" || identifier == "UFUNCTION" || identifier == "UCLASS" ||
                     identifier == "USTRUCT" || identifier == "UENUM" || identifier == "UDELEGATE" ||
                     identifier == "GENERATED_BODY" || identifier == "GENERATED_UCLASS_BODY")
            {
                // Compiler and engine attribute specifiers
                append(identifier, UI::Editor::EditorTokenKind::Directive);
            }
            else if (grammar.is_keyword(identifier))
            {
                append(identifier, UI::Editor::EditorTokenKind::Keyword);
                if (identifier == "namespace" || identifier == "package" || identifier == "import" ||
                    identifier == "mod" || identifier == "use")
                {
                    decl_context = DeclContext::Namespace;
                }
                else if (identifier == "class" || identifier == "struct" || identifier == "interface" ||
                         identifier == "enum" || identifier == "trait" || identifier == "record" || identifier == "union" ||
                         identifier == "typename")
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
            else if ((preceded_by_dot || preceded_by_arrow) && !is_jsx_or_html)
            {
                // Object member variable or method access (e.g. player.Health, mesh->Location, this->m_value)
                if (followed_by_paren)
                {
                    append(identifier, UI::Editor::EditorTokenKind::Label);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Plain);
                }
            }
            else if (decl_context == DeclContext::Namespace || decl_context == DeclContext::Class)
            {
                append(identifier, UI::Editor::EditorTokenKind::Label);
                if (decl_context != DeclContext::Namespace)
                {
                    decl_context = DeclContext::None;
                }
            }
            else if (followed_by_scope)
            {
                // Part of scope resolution A::B::C (e.g. EditorScrollbar::reset, Zenvra::Platform)
                append(identifier, UI::Editor::EditorTokenKind::Label);
            }
            else if ((prev_token_kind == UI::Editor::EditorTokenKind::Type ||
                      prev_token_text == "*" || prev_token_text == "&" || prev_token_text == ">") &&
                     grammar.supports_preprocessor)
            {
                // Variable or function name being declared immediately after a type/ptr/ref
                // e.g. unsigned char Assets_..., int TotalCount, FVector PlayerLocation
                if (followed_by_paren)
                {
                    append(identifier, UI::Editor::EditorTokenKind::Label);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Plain);
                }
            }
            else if (is_pascal_case_type(identifier) && grammar.name != "HTML" && grammar.name != "HTML/CSS" && grammar.name != "PHP" && grammar.name != "CSS")
            {
                // PascalCase user-defined types (e.g. StudioWorkspaceRenderer, Drawable, SidebarItem, MyComponent)
                append(identifier, UI::Editor::EditorTokenKind::Type);
            }
            else if (grammar.supports_preprocessor && identifier == "defined")
            {
                append(identifier, UI::Editor::EditorTokenKind::Directive);
            }
            else if (grammar.name == "Rust" && cursor < line.size() && line[cursor] == '!')
            {
                if (identifier == "macro_rules")
                {
                    append(identifier, UI::Editor::EditorTokenKind::Directive);
                }
                else
                {
                    append(identifier, UI::Editor::EditorTokenKind::Macro);
                }
                ++cursor;
                append("!", UI::Editor::EditorTokenKind::Directive);
            }
            else if (is_all_caps_constant(identifier) && grammar.name != "HTML" && grammar.name != "HTML/CSS" && grammar.name != "CSS")
            {
                // ALL_CAPS macro constants / defines (e.g. MAX_PATH, NOMINMAX, NDEBUG, NULL)
                append(identifier, UI::Editor::EditorTokenKind::Macro);
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
                // Variables, parameters, members, plain identifiers -> WHITE (Plain)
                append(identifier, UI::Editor::EditorTokenKind::Plain);
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
