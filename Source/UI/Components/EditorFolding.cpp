#include "UI/Components/EditorFolding.h"

#include <algorithm>
#include <stack>
#include <span>

namespace Zenvra::UI::Components
{

namespace
{

/// Count leading whitespace characters (spaces and tabs, with configurable tab_size).
std::size_t measure_line_indent(std::string_view line, std::size_t tab_size)
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
            indent += tab_size;
        }
        else
        {
            break;
        }
    }
    return indent;
}

bool is_whitespace_only(std::string_view line)
{
    return line.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

/// Check whether a line contains an unmatched opening brace '{' that is not
/// inside a string or comment.  This is a simplified heuristic.
bool has_opening_brace(std::string_view line)
{
    bool in_string = false;
    bool in_char = false;
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

void EditorFoldingModel::rebuild(std::span<const std::string> lines, std::size_t tab_size)
{
    m_ranges.clear();
    const std::size_t count = lines.size();
    m_effective_indents.assign(count, 0);

    if (count == 0)
    {
        m_collapsed.clear();
        return;
    }

    if (tab_size == 0)
    {
        tab_size = 4;
    }

    // 1. Compute raw indents for non-empty lines
    std::vector<int> raw_indents(count, -1);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!is_whitespace_only(lines[i]))
        {
            raw_indents[i] = static_cast<int>(measure_line_indent(lines[i], tab_size));
        }
    }

    // 2. Propagate indentation to empty lines from surrounding non-empty lines
    int last_indent = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (raw_indents[i] >= 0)
        {
            last_indent = raw_indents[i];
            m_effective_indents[i] = static_cast<std::size_t>(raw_indents[i]);
        }
        else
        {
            int next_indent = last_indent;
            for (std::size_t j = i + 1; j < count; ++j)
            {
                if (raw_indents[j] >= 0)
                {
                    next_indent = raw_indents[j];
                    break;
                }
            }
            m_effective_indents[i] = static_cast<std::size_t>(std::max(last_indent, next_indent));
        }
    }

    // 3. Stack of (start_line, indent_level) for unmatched opening braces.
    std::stack<std::pair<std::size_t, std::size_t>> open_stack;

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string_view line{lines[i]};
        const std::size_t indent = m_effective_indents[i];

        if (has_opening_brace(line))
        {
            std::size_t fold_start = i;
            const std::size_t first_non_ws = line.find_first_not_of(" \t\r\n");
            if (first_non_ws != std::string_view::npos && line[first_non_ws] == '{' && i > 0)
            {
                const std::string_view prev_line{lines[i - 1]};
                if (!is_whitespace_only(prev_line) && !has_opening_brace(prev_line) && !has_closing_brace(prev_line))
                {
                    fold_start = i - 1;
                }
            }
            open_stack.push({fold_start, indent});
        }

        if (has_closing_brace(line) && !open_stack.empty())
        {
            const auto [start, start_indent] = open_stack.top();
            open_stack.pop();

            if (i > start)
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
        if (range && line_index > range->start_line && line_index <= range->end_line)
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

std::size_t EditorFoldingModel::get_effective_indent(std::size_t line_index) const noexcept
{
    if (line_index < m_effective_indents.size())
    {
        return m_effective_indents[line_index];
    }
    return 0;
}

ActiveIndentScope EditorFoldingModel::get_active_indent_scope(
    std::size_t caret_line,
    std::size_t tab_size) const noexcept
{
    if (caret_line >= m_effective_indents.size() || tab_size == 0)
    {
        return {};
    }

    // 1. Prioritize innermost enclosing fold range (brace scope)
    const FoldRange* best_range = nullptr;
    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line))
        {
            continue;
        }
        if (caret_line >= r.start_line && caret_line <= r.end_line)
        {
            if (best_range == nullptr || r.indent_level > best_range->indent_level ||
                (r.indent_level == best_range->indent_level && (r.end_line - r.start_line) < (best_range->end_line - best_range->start_line)))
            {
                best_range = &r;
            }
        }
    }

    if (best_range != nullptr)
    {
        const std::size_t active_col = ((best_range->indent_level / tab_size) + 1) * tab_size;
        return ActiveIndentScope{
            .start_line = best_range->start_line,
            .end_line = best_range->end_line,
            .column = active_col,
            .valid = true
        };
    }

    // 2. Fallback to indent-based scope scanning
    const std::size_t caret_indent = m_effective_indents[caret_line];
    if (caret_indent < tab_size)
    {
        return {};
    }

    const std::size_t active_col = (caret_indent / tab_size) * tab_size;

    std::size_t start = caret_line;
    while (start > 0 && m_effective_indents[start - 1] >= active_col)
    {
        --start;
    }
    if (start > 0 && m_effective_indents[start - 1] < active_col)
    {
        --start;
    }

    std::size_t end = caret_line;
    const std::size_t total = m_effective_indents.size();
    while (end + 1 < total && m_effective_indents[end + 1] >= active_col)
    {
        ++end;
    }
    if (end + 1 < total && m_effective_indents[end + 1] < active_col)
    {
        ++end;
    }

    return ActiveIndentScope{
        .start_line = start,
        .end_line = end,
        .column = active_col,
        .valid = true
    };
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
