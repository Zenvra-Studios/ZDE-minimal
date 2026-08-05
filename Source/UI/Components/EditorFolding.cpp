#include "UI/Components/EditorFolding.h"

#include <algorithm>
#include <stack>

namespace Zenvra::UI::Components
{

namespace
{

/// Count leading whitespace characters (spaces and tabs, where tab = 4 spaces).
std::size_t measure_indent(std::string_view line)
{
    std::size_t indent = 0;
    for (char ch : line)
    {
        if (ch == ' ')
        {
            ++indent;
        }
        else if (ch == '\t')
        {
            indent += 4;
        }
        else
        {
            break;
        }
    }
    return indent;
}

/// Check whether a line contains an unmatched opening brace '{' that is not
/// inside a string or comment.  This is a simplified heuristic.
bool has_opening_brace(std::string_view line)
{
    bool in_string = false;
    bool in_char = false;
    char quote = '\0';
    int brace_delta = 0;

    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char ch = line[i];

        // Skip line comments.
        if (!in_string && !in_char && ch == '/' && i + 1 < line.size() && line[i + 1] == '/')
        {
            break;
        }

        // String/char literal tracking.
        if (!in_string && !in_char && (ch == '"' || ch == '\''))
        {
            if (ch == '"') in_string = true;
            else in_char = true;
            quote = ch;
            continue;
        }
        if ((in_string || in_char) && ch == '\\' && i + 1 < line.size())
        {
            ++i; // skip escaped character
            continue;
        }
        if (in_string && ch == '"')
        {
            in_string = false;
            continue;
        }
        if (in_char && ch == '\'')
        {
            in_char = false;
            continue;
        }

        if (!in_string && !in_char)
        {
            if (ch == '{') ++brace_delta;
            else if (ch == '}') --brace_delta;
        }
    }
    return brace_delta > 0;
}

bool has_closing_brace(std::string_view line)
{
    // Simplified: the trimmed line starts with '}'.
    const std::size_t first = line.find_first_not_of(" \t");
    return first != std::string_view::npos && line[first] == '}';
}

} // namespace

void EditorFoldingModel::rebuild(const std::vector<std::string>& lines)
{
    m_ranges.clear();

    // Stack of (start_line, indent_level) for unmatched opening braces.
    std::stack<std::pair<std::size_t, std::size_t>> open_stack;

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line{lines[i]};
        const std::size_t indent = measure_indent(line);

        if (has_opening_brace(line))
        {
            open_stack.push({i, indent});
        }

        if (has_closing_brace(line) && !open_stack.empty())
        {
            const auto [start, start_indent] = open_stack.top();
            open_stack.pop();

            // Only create a range if there is at least one line between
            // the opening and closing braces.
            if (i > start + 1)
            {
                m_ranges.push_back(FoldRange{start, i, start_indent});
            }
        }
    }

    // Sort ranges by start_line for efficient lookup.
    std::sort(m_ranges.begin(), m_ranges.end(),
        [](const FoldRange& a, const FoldRange& b) {
            return a.start_line < b.start_line;
        });

    // Prune collapsed entries that no longer correspond to valid ranges.
    std::unordered_set<std::size_t> valid_collapsed;
    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line))
        {
            valid_collapsed.insert(r.start_line);
        }
    }
    m_collapsed = std::move(valid_collapsed);
}

bool EditorFoldingModel::toggle_fold(std::size_t line_index)
{
    // Verify that this line is actually the start of a range.
    bool found = false;
    for (const auto& r : m_ranges)
    {
        if (r.start_line == line_index)
        {
            found = true;
            break;
        }
    }
    if (!found) return false;

    if (m_collapsed.count(line_index))
    {
        m_collapsed.erase(line_index);
    }
    else
    {
        m_collapsed.insert(line_index);
    }
    return true;
}

bool EditorFoldingModel::is_line_hidden(std::size_t line_index) const noexcept
{
    for (const std::size_t start : m_collapsed)
    {
        const FoldRange* range = get_range_at(start);
        if (range && line_index > range->start_line && line_index < range->end_line)
        {
            return true;
        }
    }
    return false;
}

FoldMarker EditorFoldingModel::get_marker(std::size_t line_index) const noexcept
{
    // Check if this line is the start of a fold range.
    for (const auto& r : m_ranges)
    {
        if (r.start_line == line_index)
        {
            return m_collapsed.count(line_index)
                ? FoldMarker::Collapsed
                : FoldMarker::Expanded;
        }
    }

    // Check if this line is a continuation or end of an expanded range.
    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line))
        {
            continue; // Skip collapsed ranges.
        }
        if (line_index > r.start_line && line_index < r.end_line)
        {
            return FoldMarker::Continuation;
        }
        if (line_index == r.end_line)
        {
            return FoldMarker::End;
        }
    }

    return FoldMarker::NoneMarker;
}

const std::vector<FoldRange>& EditorFoldingModel::get_ranges() const noexcept
{
    return m_ranges;
}

const std::unordered_set<std::size_t>& EditorFoldingModel::get_collapsed() const noexcept
{
    return m_collapsed;
}

const FoldRange* EditorFoldingModel::get_range_at(std::size_t line_index) const noexcept
{
    for (const auto& r : m_ranges)
    {
        if (r.start_line == line_index) return &r;
    }
    return nullptr;
}

bool EditorFoldingModel::is_fold_start(std::size_t line_index) const noexcept
{
    for (const auto& r : m_ranges)
    {
        if (r.start_line == line_index) return true;
    }
    return false;
}

std::vector<const FoldRange*> EditorFoldingModel::get_indent_guide_ranges(
    std::size_t first_line,
    std::size_t last_line) const
{
    std::vector<const FoldRange*> result;
    for (const auto& r : m_ranges)
    {
        // Skip collapsed ranges and ranges hidden by parent folds.
        if (m_collapsed.count(r.start_line) || is_line_hidden(r.start_line))
        {
            continue;
        }
        // Include if the range overlaps the viewport.
        if (r.end_line >= first_line && r.start_line <= last_line)
        {
            result.push_back(&r);
        }
    }
    return result;
}

const FoldRange* EditorFoldingModel::get_active_indent_range(
    std::size_t line_index) const noexcept
{
    const FoldRange* best = nullptr;
    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line))
        {
            continue;
        }
        if (line_index >= r.start_line && line_index <= r.end_line)
        {
            // Pick the innermost (deepest) range.
            if (best == nullptr || r.indent_level > best->indent_level)
            {
                best = &r;
            }
        }
    }
    return best;
}

} // namespace Zenvra::UI::Components
