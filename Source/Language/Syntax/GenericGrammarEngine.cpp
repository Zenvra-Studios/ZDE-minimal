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
        const std::string_view end_token = !grammar.block_comment_end.empty()
            ? std::string_view(grammar.block_comment_end)
            : std::string_view("*/");
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

        // 1. Preprocessor (e.g. #include, #define, #pragma in C/C++)
        if (grammar.supports_preprocessor && character == '#')
        {
            std::size_t scan = cursor;
            bool in_quotes = false;
            char quote_char = 0;
            std::size_t comment_start = std::string_view::npos;
            bool is_block_comment = false;

            while (scan < line.size())
            {
                const char c = line[scan];
                if (in_quotes)
                {
                    if (c == '\\' && scan + 1 < line.size())
                    {
                        scan += 2;
                        continue;
                    }
                    if (c == quote_char)
                    {
                        in_quotes = false;
                    }
                    ++scan;
                    continue;
                }

                if (c == '"' || c == '\'')
                {
                    in_quotes = true;
                    quote_char = c;
                    ++scan;
                    continue;
                }

                if (!grammar.line_comment.empty() && line.substr(scan).starts_with(grammar.line_comment))
                {
                    comment_start = scan;
                    is_block_comment = false;
                    break;
                }

                if (!grammar.block_comment_start.empty() && line.substr(scan).starts_with(grammar.block_comment_start))
                {
                    comment_start = scan;
                    is_block_comment = true;
                    break;
                }

                ++scan;
            }

            if (comment_start == std::string_view::npos)
            {
                append(line.substr(cursor), UI::Editor::EditorTokenKind::Keyword);
                break;
            }
            else
            {
                if (comment_start > cursor)
                {
                    append(line.substr(cursor, comment_start - cursor), UI::Editor::EditorTokenKind::Keyword);
                }

                if (!is_block_comment)
                {
                    append(line.substr(comment_start), UI::Editor::EditorTokenKind::Comment);
                    break;
                }
                else
                {
                    cursor = comment_start + grammar.block_comment_start.size();
                    const std::size_t end_pos = line.find(grammar.block_comment_end, cursor);
                    if (end_pos != std::string_view::npos)
                    {
                        cursor = end_pos + grammar.block_comment_end.size();
                    }
                    else
                    {
                        cursor = line.size();
                        state.kind = TokenizerState::StateKind::BlockComment;
                    }
                    append(line.substr(comment_start, cursor - comment_start), UI::Editor::EditorTokenKind::Comment);
                    if (state.kind == TokenizerState::StateKind::BlockComment)
                    {
                        return token_count;
                    }
                    continue;
                }
            }
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
            (grammar.name == "Assembly" && (character == ';' || character == '@' || line.substr(cursor).starts_with("//"))))
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
                append(line.substr(token_start, cursor - token_start), UI::Editor::EditorTokenKind::Comment);
                continue;
            }
            else
            {
                append(line.substr(token_start), UI::Editor::EditorTokenKind::Comment);
                state.kind = TokenizerState::StateKind::BlockComment;
                return token_count;
            }
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

        // 10. Identifiers (e.g. Zenvra, EditorScrollbar, reset, std)
        if (std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_')
        {
            while (cursor < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[cursor])) != 0 || line[cursor] == '_'))
            {
                ++cursor;
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
                append(identifier, UI::Editor::EditorTokenKind::Label);
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
