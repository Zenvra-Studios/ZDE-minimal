#include "UI/Editor/CppSymbolLexer.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::UI::Editor
{

namespace
{

/// Returns true if `ch` is a valid C++ identifier character.
bool is_identifier_char(char ch)
{
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

/// Returns true if `ch` could appear in a qualified name (e.g. `Foo::Bar`).
bool is_qualified_name_char(char ch)
{
    return is_identifier_char(ch) || ch == ':';
}

/// Trims leading and trailing whitespace from a string view.
std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    {
        text.remove_suffix(1);
    }
    return text;
}

/// Strips a potential single-line or trailing comment from a line.
std::string_view strip_comment(std::string_view line)
{
    bool in_string = false;
    bool in_char = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        const char ch = line[index];
        if (ch == '\\' && (in_string || in_char))
        {
            ++index; // skip escaped character
            continue;
        }
        if (ch == '"' && !in_char)
        {
            in_string = !in_string;
        }
        else if (ch == '\'' && !in_string)
        {
            in_char = !in_char;
        }
        else if (!in_string && !in_char && ch == '/' && index + 1 < line.size())
        {
            if (line[index + 1] == '/')
            {
                return line.substr(0, index);
            }
        }
    }
    return line;
}

/// Extracts the last qualified identifier from a text fragment.
/// For example, from `namespace Foo::Bar` this returns `Foo::Bar`.
std::string_view extract_qualified_name(std::string_view text)
{
    text = trim(text);
    if (text.empty())
    {
        return {};
    }
    std::size_t end = text.size();
    while (end > 0 && is_qualified_name_char(text[end - 1]))
    {
        --end;
    }
    std::string_view result = text.substr(end);
    // Clean up leading/trailing colons.
    while (result.starts_with(':'))
    {
        result.remove_prefix(1);
    }
    while (result.ends_with(':'))
    {
        result.remove_suffix(1);
    }
    return result;
}

/// Attempts to match a C++ keyword at the start of a (trimmed) line.
/// Returns the keyword kind and the name following it.
struct ScopeMatch
{
    BreadcrumbIconKind kind = BreadcrumbIconKind::File;
    std::string_view name;
    bool matched = false;
};

ScopeMatch try_match_scope_keyword(std::string_view line)
{
    line = trim(strip_comment(line));
    if (line.empty())
    {
        return {};
    }

    // Check for namespace (possibly nested: namespace Foo::Bar::Baz {)
    if (line.starts_with("namespace"))
    {
        std::string_view rest = trim(line.substr(9));
        // Skip anonymous namespace
        if (rest.empty() || rest.front() == '{')
        {
            return {};
        }
        std::string_view name = extract_qualified_name(rest.substr(0, rest.find('{')));
        if (!name.empty())
        {
            return {BreadcrumbIconKind::Namespace, name, true};
        }
    }

    // Check for class/struct
    auto try_class_struct = [&](std::string_view keyword, BreadcrumbIconKind kind) -> ScopeMatch
    {
        if (!line.starts_with(keyword))
        {
            return {};
        }
        std::string_view rest = line.substr(keyword.size());
        if (rest.empty() || !std::isspace(static_cast<unsigned char>(rest.front())))
        {
            return {};
        }
        rest = trim(rest);
        // Skip forward declarations (lines ending with ;)
        if (rest.find(';') != std::string_view::npos)
        {
            return {};
        }
        // Extract name (stop at : for inheritance, { for body, or whitespace)
        std::size_t name_end = 0;
        while (name_end < rest.size() && is_identifier_char(rest[name_end]))
        {
            ++name_end;
        }
        if (name_end > 0)
        {
            return {kind, rest.substr(0, name_end), true};
        }
        return {};
    };

    ScopeMatch result = try_class_struct("class", BreadcrumbIconKind::Class);
    if (result.matched)
    {
        return result;
    }
    result = try_class_struct("struct", BreadcrumbIconKind::Struct);
    if (result.matched)
    {
        return result;
    }

    return {};
}

/// Detects if a line looks like a function definition.
/// Heuristic: has a return type, function name, parentheses, and either { or const.
/// Returns the function name if detected.
std::string_view try_match_function(std::string_view line)
{
    line = trim(strip_comment(line));
    if (line.empty())
    {
        return {};
    }

    // Skip lines that are just closing braces, preprocessor directives, etc.
    if (line.front() == '#' || line.front() == '}' || line.front() == '{')
    {
        return {};
    }

    // Skip lines that start with keywords that aren't functions.
    static constexpr std::string_view non_function_keywords[] = {
        "if", "else", "for", "while", "do", "switch", "case", "return",
        "namespace", "class", "struct", "enum", "typedef", "using",
        "public:", "private:", "protected:", "template",
    };
    for (const std::string_view keyword : non_function_keywords)
    {
        if (line.starts_with(keyword))
        {
            if (keyword.back() == ':' || line.size() == keyword.size() ||
                !is_identifier_char(line[keyword.size()]))
            {
                return {};
            }
        }
    }

    // Look for the opening parenthesis.
    const std::size_t paren_pos = line.find('(');
    if (paren_pos == std::string_view::npos || paren_pos == 0)
    {
        return {};
    }

    // Extract the function name just before the parenthesis.
    std::size_t name_end = paren_pos;
    while (name_end > 0 && std::isspace(static_cast<unsigned char>(line[name_end - 1])))
    {
        --name_end;
    }
    std::size_t name_start = name_end;
    while (name_start > 0 && is_qualified_name_char(line[name_start - 1]))
    {
        --name_start;
    }
    if (name_start == name_end)
    {
        return {};
    }

    std::string_view full_name = line.substr(name_start, name_end - name_start);
    // Clean up leading/trailing colons.
    while (full_name.starts_with(':'))
    {
        full_name.remove_prefix(1);
    }

    if (full_name.empty())
    {
        return {};
    }

    // If the name contains ::, extract just the method name after the last ::
    const std::size_t last_scope = full_name.rfind("::");
    if (last_scope != std::string_view::npos && last_scope + 2 < full_name.size())
    {
        return full_name.substr(last_scope + 2);
    }
    return full_name;
}

} // namespace

std::vector<BreadcrumbItem> CppSymbolLexer::resolve_scopes(
    std::span<const std::string> lines,
    std::size_t caret_line)
{
    std::vector<BreadcrumbItem> scopes;
    if (lines.empty())
    {
        return scopes;
    }
    caret_line = std::min(caret_line, lines.size() - 1);

    // Track brace depth as we scan backwards from the caret.
    // We start at 0 and decrement for } and increment for {.
    // When brace_depth < 0 we know we're inside a scope whose opening brace
    // is above us.
    int brace_depth = 0;

    // First pass: find the function we are currently in.
    // Scan backwards from the caret line looking for a function definition
    // at brace_depth == -1 (meaning we're one level inside a scope).
    std::string_view enclosing_function;
    {
        int local_depth = 0;
        for (std::size_t index = caret_line + 1; index > 0; --index)
        {
            const std::string_view line = trim(strip_comment(lines[index - 1]));
            // Count braces on this line (naive but sufficient for most C++ code).
            for (const char ch : line)
            {
                if (ch == '}')
                {
                    ++local_depth;
                }
                else if (ch == '{')
                {
                    --local_depth;
                }
            }
            if (local_depth < 0)
            {
                // We've found the opening brace of the scope we're in.
                // Check if this line (or the lines above it) define a function.
                std::string_view func = try_match_function(line);
                if (func.empty() && index >= 2)
                {
                    // The function signature might be on the previous line(s).
                    func = try_match_function(lines[index - 2]);
                    if (func.empty() && index >= 3)
                    {
                        func = try_match_function(lines[index - 3]);
                    }
                }
                enclosing_function = func;
                break;
            }
        }
    }

    // Second pass: find enclosing namespace/class/struct scopes.
    brace_depth = 0;
    for (std::size_t index = caret_line + 1; index > 0; --index)
    {
        const std::string_view line = trim(strip_comment(lines[index - 1]));

        for (const char ch : line)
        {
            if (ch == '}')
            {
                ++brace_depth;
            }
            else if (ch == '{')
            {
                --brace_depth;
            }
        }

        if (brace_depth < 0)
        {
            // We just passed through an unmatched opening brace.
            // Try matching a scope keyword on this line or the line above.
            ScopeMatch match = try_match_scope_keyword(line);
            if (!match.matched && index >= 2)
            {
                match = try_match_scope_keyword(lines[index - 2]);
            }
            if (match.matched)
            {
                scopes.push_back({std::string{match.name}, match.kind});
            }
            brace_depth = 0; // reset to look for the next enclosing scope
        }
    }

    // The scopes were collected inner-to-outer, reverse them.
    std::reverse(scopes.begin(), scopes.end());

    // Add the enclosing function at the end.
    if (!enclosing_function.empty())
    {
        scopes.push_back({std::string{enclosing_function}, BreadcrumbIconKind::Function});
    }

    return scopes;
}

} // namespace Zenvra::UI::Editor
