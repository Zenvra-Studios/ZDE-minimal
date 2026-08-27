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
    if (line.empty() || (line[0] != ' ' && line[0] != '\t'))
    {
        return 0;
    }
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
    return line.empty() || line.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

} // namespace

void EditorFoldingModel::rebuild(std::span<const std::string> lines, std::size_t tab_size,
                                 std::size_t focus_center, std::size_t focus_radius)
{
    m_ranges.clear();
    const std::size_t total_count = lines.size();

    if (total_count == 0)
    {
        m_collapsed.clear();
        m_effective_indents.clear();
        m_window_offset = 0;
        return;
    }

    if (tab_size == 0)
    {
        tab_size = 4;
    }

    std::size_t start_idx = 0;
    std::size_t end_idx = total_count;

    if (total_count > 25000 && focus_radius > 0)
    {
        start_idx = (focus_center > focus_radius) ? (focus_center - focus_radius) : 0;
        end_idx = std::min(total_count, focus_center + focus_radius);
    }

    m_window_offset = start_idx;
    const std::size_t count = end_idx - start_idx;
    m_effective_indents.assign(count, 0);

    // 1. Compute raw indents for non-empty lines in window
    m_raw_indents.assign(count, -1);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& line_str = lines[start_idx + i];
        if (!is_whitespace_only(line_str))
        {
            m_raw_indents[i] = static_cast<int>(measure_line_indent(line_str, tab_size));
        }
    }

    // 2. Propagate indentation to empty lines from surrounding non-empty lines in O(N)
    m_next_indents.assign(count, 0);
    int next_val = 0;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(count) - 1; i >= 0; --i)
    {
        if (m_raw_indents[static_cast<std::size_t>(i)] >= 0)
        {
            next_val = m_raw_indents[static_cast<std::size_t>(i)];
        }
        m_next_indents[static_cast<std::size_t>(i)] = next_val;
    }

    int last_indent = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (m_raw_indents[i] >= 0)
        {
            last_indent = m_raw_indents[i];
            m_effective_indents[i] = static_cast<std::size_t>(m_raw_indents[i]);
        }
        else
        {
            m_effective_indents[i] = static_cast<std::size_t>(std::max(last_indent, m_next_indents[i]));
        }
    }

    // 3. Sequential character-by-character scanner for curly braces
    // Stack of (start_line, indent_level)
    std::stack<std::pair<std::size_t, std::size_t>> open_stack;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < count; ++i)
    {
        const std::size_t global_line = start_idx + i;
        const std::string_view line{lines[global_line]};
        const std::size_t indent = m_effective_indents[i];

        bool in_string = false;
        char string_quote = 0;

        for (std::size_t col = 0; col < line.size(); ++col)
        {
            const char ch = line[col];

            // Inside block comment /* ... */
            if (in_block_comment)
            {
                if (ch == '*' && col + 1 < line.size() && line[col + 1] == '/')
                {
                    in_block_comment = false;
                    ++col; // skip '/'
                }
                continue;
            }

            // Inside string literal ("...", '...', `...`)
            if (in_string)
            {
                if (ch == '\\' && col + 1 < line.size())
                {
                    ++col; // skip escaped character
                    continue;
                }
                if (ch == string_quote)
                {
                    in_string = false;
                    string_quote = 0;
                }
                continue;
            }

            // Line comment (// or #)
            if (ch == '/' && col + 1 < line.size() && line[col + 1] == '/')
            {
                break; // rest of the line is a comment
            }
            if (ch == '#')
            {
                break; // python/shell/yaml line comment
            }

            // Block comment start /*
            if (ch == '/' && col + 1 < line.size() && line[col + 1] == '*')
            {
                in_block_comment = true;
                ++col; // skip '*'
                continue;
            }

            // String start
            if (ch == '"' || ch == '\'' || ch == '`')
            {
                in_string = true;
                string_quote = ch;
                continue;
            }

            // Curly brace closing }
            if (ch == '}')
            {
                if (!open_stack.empty())
                {
                    const auto [start, start_indent] = open_stack.top();
                    open_stack.pop();

                    if (global_line > start)
                    {
                        m_ranges.push_back(FoldRange{start, global_line, start_indent});
                    }
                }
                continue;
            }

            // Curly brace opening {
            if (ch == '{')
            {
                std::size_t fold_start = global_line;
                const std::size_t first_non_ws = line.find_first_not_of(" \t\r\n");
                // If '{' is the first non-whitespace character on this line, check if we should attach to previous header line
                if (first_non_ws == col && global_line > 0 && i > 0)
                {
                    const std::string_view prev_line{lines[global_line - 1]};
                    if (!is_whitespace_only(prev_line) &&
                        prev_line.find('{') == std::string_view::npos &&
                        prev_line.find('}') == std::string_view::npos)
                    {
                        fold_start = global_line - 1;
                    }
                }
                open_stack.push({fold_start, indent});
                continue;
            }
        }
    }

    // 4. Universal indentation-based fold ranges for all programming languages
    // (Python, YAML, Makefile, HTML, Lua, Ruby, Shell, Markdown, etc.)
    std::unordered_set<std::size_t> brace_starts;
    for (const auto& br : m_ranges)
    {
        brace_starts.insert(br.start_line);
    }

    std::vector<FoldRange> indent_ranges;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (m_raw_indents[i] < 0)
        {
            continue; // Skip whitespace lines
        }

        const int cur_indent = m_raw_indents[i];
        const std::size_t global_start = start_idx + i;

        // If this line already has a brace-delimited fold range starting on it, keep the brace range
        if (brace_starts.contains(global_start))
        {
            continue;
        }

        std::size_t last_child_line = i;
        bool has_child = false;

        for (std::size_t j = i + 1; j < count; ++j)
        {
            if (m_raw_indents[j] < 0)
            {
                // Blank line - keep looking ahead, but do not count trailing blank line as last child
                continue;
            }
            if (m_raw_indents[j] > cur_indent)
            {
                last_child_line = j;
                has_child = true;
            }
            else
            {
                break;
            }
        }

        if (has_child && last_child_line > i)
        {
            const std::size_t global_end = start_idx + last_child_line;
            indent_ranges.push_back(FoldRange{
                .start_line = global_start,
                .end_line = global_end,
                .indent_level = static_cast<std::size_t>(cur_indent)
            });
        }
    }

    for (auto&& ir : indent_ranges)
    {
        m_ranges.push_back(std::move(ir));
    }

    // Sort ranges by start_line for efficient lookup.
    std::sort(m_ranges.begin(), m_ranges.end(),
        [](const FoldRange& a, const FoldRange& b) {
            if (a.start_line != b.start_line)
                return a.start_line < b.start_line;
            return a.end_line < b.end_line;
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
    if (m_collapsed.empty())
    {
        return false;
    }
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
    // 1. Check if this line is the start of a fold range (binary search)
    if (const auto* r = get_range_at(line_index))
    {
        return m_collapsed.count(line_index)
            ? FoldMarker::Collapsed
            : FoldMarker::Expanded;
    }

    // 2. Check all expanded ranges that cover this line
    bool is_end = false;
    bool is_continuation = false;

    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line) || is_line_hidden(r.start_line))
        {
            continue; // Skip collapsed ranges.
        }
        if (line_index == r.end_line)
        {
            is_end = true;
        }
        else if (line_index > r.start_line && line_index < r.end_line)
        {
            is_continuation = true;
        }
    }

    if (is_end)
    {
        return FoldMarker::End;
    }
    if (is_continuation)
    {
        return FoldMarker::Continuation;
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
    if (m_ranges.empty())
    {
        return nullptr;
    }
    auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), line_index,
        [](const FoldRange& r, std::size_t line) {
            return r.start_line < line;
        });
    if (it != m_ranges.end() && it->start_line == line_index)
    {
        return &(*it);
    }
    return nullptr;
}

bool EditorFoldingModel::is_fold_start(std::size_t line_index) const noexcept
{
    return get_range_at(line_index) != nullptr;
}

std::size_t EditorFoldingModel::get_effective_indent(std::size_t line_index) const noexcept
{
    if (line_index >= m_window_offset && (line_index - m_window_offset) < m_effective_indents.size())
    {
        return m_effective_indents[line_index - m_window_offset];
    }
    return 0;
}

ActiveIndentScope EditorFoldingModel::get_active_indent_scope(
    std::size_t caret_line,
    std::size_t tab_size) const noexcept
{
    if (caret_line < m_window_offset || (caret_line - m_window_offset) >= m_effective_indents.size() || tab_size == 0)
    {
        return {};
    }

    // 1. Prioritize innermost enclosing fold range (brace scope)
    const FoldRange* best_range = nullptr;
    for (const auto& r : m_ranges)
    {
        if (m_collapsed.count(r.start_line) || is_line_hidden(r.start_line))
        {
            continue;
        }
        if (caret_line >= r.start_line && caret_line <= r.end_line)
        {
            if (best_range == nullptr ||
                r.indent_level > best_range->indent_level ||
                (r.indent_level == best_range->indent_level && (r.end_line - r.start_line) < (best_range->end_line - best_range->start_line)))
            {
                best_range = &r;
            }
        }
    }

    if (best_range != nullptr)
    {
        const std::size_t active_col = best_range->indent_level;
        return ActiveIndentScope{
            .start_line = best_range->start_line,
            .end_line = best_range->end_line,
            .column = active_col,
            .valid = true
        };
    }

    // 2. Fallback to indent-based scope scanning (bounded to viewport neighborhood)
    const std::size_t rel_caret = caret_line - m_window_offset;
    const std::size_t caret_indent = m_effective_indents[rel_caret];
    if (caret_indent < tab_size)
    {
        return {};
    }

    const std::size_t active_col = (caret_indent >= tab_size) ? ((caret_indent - 1) / tab_size) * tab_size : 0;
    const std::size_t min_search = rel_caret > 200 ? rel_caret - 200 : 0;
    const std::size_t max_search = std::min(m_effective_indents.size(), rel_caret + 200);

    std::size_t start = rel_caret;
    while (start > min_search && m_effective_indents[start - 1] > active_col)
    {
        --start;
    }
    if (start > min_search && m_effective_indents[start - 1] == active_col)
    {
        --start;
    }

    std::size_t end = rel_caret;
    while (end + 1 < max_search && m_effective_indents[end + 1] > active_col)
    {
        ++end;
    }

    return ActiveIndentScope{
        .start_line = start + m_window_offset,
        .end_line = end + m_window_offset,
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
