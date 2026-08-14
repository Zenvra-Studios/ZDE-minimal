#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Zenvra::Terminal
{

struct TerminalPosition
{
    std::size_t line = 0;
    std::size_t column = 0;

    [[nodiscard]] constexpr bool operator==(const TerminalPosition& other) const noexcept
    {
        return line == other.line && column == other.column;
    }

    [[nodiscard]] constexpr bool operator!=(const TerminalPosition& other) const noexcept
    {
        return !(*this == other);
    }

    [[nodiscard]] constexpr bool operator<(const TerminalPosition& other) const noexcept
    {
        if (line != other.line)
        {
            return line < other.line;
        }
        return column < other.column;
    }

    [[nodiscard]] constexpr bool operator<=(const TerminalPosition& other) const noexcept
    {
        return !(other < *this);
    }

    [[nodiscard]] constexpr bool operator>(const TerminalPosition& other) const noexcept
    {
        return other < *this;
    }

    [[nodiscard]] constexpr bool operator>=(const TerminalPosition& other) const noexcept
    {
        return !(*this < other);
    }
};

struct TerminalSelection
{
    TerminalPosition start;
    TerminalPosition end;

    [[nodiscard]] constexpr bool is_empty() const noexcept
    {
        return start == end;
    }

    [[nodiscard]] constexpr TerminalPosition normalized_start() const noexcept
    {
        return start < end ? start : end;
    }

    [[nodiscard]] constexpr TerminalPosition normalized_end() const noexcept
    {
        return start < end ? end : start;
    }

    [[nodiscard]] constexpr bool contains(std::size_t line_index, std::size_t col_index) const noexcept
    {
        if (is_empty())
        {
            return false;
        }
        const TerminalPosition n_start = normalized_start();
        const TerminalPosition n_end = normalized_end();
        const TerminalPosition pos{line_index, col_index};
        return pos >= n_start && pos < n_end;
    }

    [[nodiscard]] bool intersects_line(std::size_t line_index) const noexcept
    {
        if (is_empty())
        {
            return false;
        }
        const TerminalPosition n_start = normalized_start();
        const TerminalPosition n_end = normalized_end();
        return line_index >= n_start.line && line_index <= n_end.line;
    }

    [[nodiscard]] std::pair<std::size_t, std::size_t> get_line_range(
        std::size_t line_index,
        std::size_t line_length) const noexcept
    {
        if (!intersects_line(line_index))
        {
            return {0, 0};
        }
        const TerminalPosition n_start = normalized_start();
        const TerminalPosition n_end = normalized_end();

        const std::size_t col_start = (line_index == n_start.line)
            ? std::min(n_start.column, line_length)
            : 0;
        const std::size_t col_end = (line_index == n_end.line)
            ? std::min(n_end.column, line_length)
            : line_length;

        if (col_end > col_start)
        {
            return {col_start, col_end};
        }
        return {0, 0};
    }

    [[nodiscard]] std::string extract_text(std::span<const std::string> lines) const
    {
        if (is_empty() || lines.empty())
        {
            return {};
        }
        const TerminalPosition n_start = normalized_start();
        const TerminalPosition n_end = normalized_end();

        std::string result;
        const std::size_t start_line = std::min(n_start.line, lines.size() - 1);
        const std::size_t end_line = std::min(n_end.line, lines.size() - 1);

        for (std::size_t idx = start_line; idx <= end_line; ++idx)
        {
            const std::string& line = lines[idx];
            const std::size_t col_start = (idx == n_start.line)
                ? std::min(n_start.column, line.size())
                : 0;
            const std::size_t col_end = (idx == n_end.line)
                ? std::min(n_end.column, line.size())
                : line.size();

            if (col_end > col_start)
            {
                result.append(line.substr(col_start, col_end - col_start));
            }
            if (idx < end_line)
            {
                result.push_back('\n');
            }
        }
        return result;
    }
};

} // namespace Zenvra::Terminal
