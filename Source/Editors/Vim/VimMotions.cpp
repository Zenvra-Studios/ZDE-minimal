#include "Editors/Vim/VimMotions.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Editors::VimMotions
{

namespace
{

enum class CharClass
{
    Word,
    Punctuation,
    Whitespace
};

CharClass classify(char c) noexcept
{
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::isspace(uc))
    {
        return CharClass::Whitespace;
    }
    if (std::isalnum(uc) || c == '_')
    {
        return CharClass::Word;
    }
    return CharClass::Punctuation;
}

} // namespace

void move_left(TextDocumentModel& doc, int count, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::size_t step = static_cast<std::size_t>(std::max(1, count));

    const std::size_t new_col = (col >= step) ? (col - step) : 0;
    doc.set_caret(line, new_col, extend_selection);
}

void move_right(TextDocumentModel& doc, int count, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::string_view l = doc.get_line(line);
    const std::size_t step = static_cast<std::size_t>(std::max(1, count));

    const std::size_t max_col = l.empty() ? 0 : l.size() - 1;
    const std::size_t new_col = std::min(col + step, max_col);
    doc.set_caret(line, new_col, extend_selection);
}

void move_up(TextDocumentModel& doc, int count, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::size_t step = static_cast<std::size_t>(std::max(1, count));

    const std::size_t new_line = (line >= step) ? (line - step) : 0;
    const std::string_view l = doc.get_line(new_line);
    const std::size_t new_col = std::min(col, l.empty() ? 0 : l.size() - 1);
    doc.set_caret(new_line, new_col, extend_selection);
}

void move_down(TextDocumentModel& doc, int count, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::size_t max_line = doc.get_line_count() - 1;
    const std::size_t step = static_cast<std::size_t>(std::max(1, count));

    const std::size_t new_line = std::min(line + step, max_line);
    const std::string_view l = doc.get_line(new_line);
    const std::size_t new_col = std::min(col, l.empty() ? 0 : l.size() - 1);
    doc.set_caret(new_line, new_col, extend_selection);
}

void move_line_start(TextDocumentModel& doc, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    doc.set_caret(doc.get_caret_line(), 0, extend_selection);
}

void move_first_non_blank(TextDocumentModel& doc, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::string_view l = doc.get_line(line);

    std::size_t col = 0;
    while (col < l.size() && std::isspace(static_cast<unsigned char>(l[col])))
    {
        ++col;
    }
    doc.set_caret(line, col, extend_selection);
}

void move_line_end(TextDocumentModel& doc, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t line = doc.get_caret_line();
    const std::string_view l = doc.get_line(line);
    const std::size_t max_col = l.empty() ? 0 : (l.size() > 0 ? l.size() - 1 : 0);
    doc.set_caret(line, max_col, extend_selection);
}

TextPosition get_word_forward_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t total_lines = doc.get_line_count();
    const int steps = std::max(1, count);

    std::size_t cur_line = std::min(start.line, total_lines - 1);
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        std::string_view l = doc.get_line(cur_line);
        if (cur_col >= l.size())
        {
            if (cur_line + 1 < total_lines)
            {
                ++cur_line;
                cur_col = 0;
                l = doc.get_line(cur_line);
            }
            else
            {
                break;
            }
        }

        if (!l.empty() && cur_col < l.size())
        {
            const CharClass initial_class = classify(l[cur_col]);
            if (initial_class != CharClass::Whitespace)
            {
                while (cur_col < l.size() && classify(l[cur_col]) == initial_class)
                {
                    ++cur_col;
                }
            }

            // Skip following whitespace
            while (cur_line < total_lines)
            {
                l = doc.get_line(cur_line);
                while (cur_col < l.size() && std::isspace(static_cast<unsigned char>(l[cur_col])))
                {
                    ++cur_col;
                }

                if (cur_col < l.size())
                {
                    break;
                }

                if (cur_line + 1 < total_lines)
                {
                    ++cur_line;
                    cur_col = 0;
                    l = doc.get_line(cur_line);
                    if (l.empty())
                    {
                        break;
                    }
                }
                else
                {
                    cur_col = l.empty() ? 0 : l.size() - 1;
                    break;
                }
            }
        }
        else if (cur_line + 1 < total_lines)
        {
            ++cur_line;
            cur_col = 0;
        }
    }

    return TextPosition{cur_line, cur_col};
}

TextPosition get_word_end_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t total_lines = doc.get_line_count();
    const int steps = std::max(1, count);

    std::size_t cur_line = std::min(start.line, total_lines - 1);
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        std::string_view l = doc.get_line(cur_line);

        // Advance past current char
        if (cur_col + 1 < l.size())
        {
            ++cur_col;
        }
        else if (cur_line + 1 < total_lines)
        {
            ++cur_line;
            cur_col = 0;
            l = doc.get_line(cur_line);
        }
        else
        {
            break;
        }

        // Skip whitespace
        while (cur_line < total_lines)
        {
            l = doc.get_line(cur_line);
            while (cur_col < l.size() && std::isspace(static_cast<unsigned char>(l[cur_col])))
            {
                ++cur_col;
            }
            if (cur_col < l.size())
            {
                break;
            }
            if (cur_line + 1 < total_lines)
            {
                ++cur_line;
                cur_col = 0;
            }
            else
            {
                break;
            }
        }

        // Advance to end of current word class
        l = doc.get_line(cur_line);
        if (cur_col < l.size())
        {
            const CharClass cls = classify(l[cur_col]);
            while (cur_col + 1 < l.size() && classify(l[cur_col + 1]) == cls)
            {
                ++cur_col;
            }
        }
    }

    return TextPosition{cur_line, cur_col};
}

TextPosition get_word_backward_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const int steps = std::max(1, count);

    std::size_t cur_line = start.line;
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        if (cur_col == 0)
        {
            if (cur_line > 0)
            {
                --cur_line;
                std::string_view l = doc.get_line(cur_line);
                cur_col = l.empty() ? 0 : l.size() - 1;
            }
            else
            {
                break;
            }
        }
        else
        {
            --cur_col;
        }

        // Skip whitespace backwards
        while (true)
        {
            std::string_view l = doc.get_line(cur_line);
            if (!l.empty() && cur_col < l.size())
            {
                while (cur_col > 0 && std::isspace(static_cast<unsigned char>(l[cur_col])))
                {
                    --cur_col;
                }
                if (!std::isspace(static_cast<unsigned char>(l[cur_col])))
                {
                    break;
                }
            }

            if (cur_line > 0)
            {
                --cur_line;
                l = doc.get_line(cur_line);
                cur_col = l.empty() ? 0 : l.size() - 1;
            }
            else
            {
                cur_col = 0;
                break;
            }
        }

        // Now back up across the same character class
        std::string_view l = doc.get_line(cur_line);
        if (!l.empty() && cur_col < l.size())
        {
            const CharClass cls = classify(l[cur_col]);
            while (cur_col > 0 && classify(l[cur_col - 1]) == cls)
            {
                --cur_col;
            }
        }
    }

    return TextPosition{cur_line, cur_col};
}

TextPosition get_line_end_target(const TextDocumentModel& doc, TextPosition start)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t line = std::min(start.line, doc.get_line_count() - 1);
    const std::string_view l = doc.get_line(line);
    return TextPosition{line, l.size()};
}

void move_word_forward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_word_forward_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_word_backward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_word_backward_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_word_end(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_word_end_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_document_start(TextDocumentModel& doc, int line_number, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t target_line = (line_number > 0)
        ? std::min(static_cast<std::size_t>(line_number - 1), doc.get_line_count() - 1)
        : 0;

    const std::string_view l = doc.get_line(target_line);
    std::size_t col = 0;
    while (col < l.size() && std::isspace(static_cast<unsigned char>(l[col])))
    {
        ++col;
    }
    doc.set_caret(target_line, col, extend_selection);
}

void move_document_end(TextDocumentModel& doc, int line_number, bool extend_selection)
{
    if (doc.get_line_count() == 0) return;
    const std::size_t target_line = (line_number > 0)
        ? std::min(static_cast<std::size_t>(line_number - 1), doc.get_line_count() - 1)
        : (doc.get_line_count() - 1);

    const std::string_view l = doc.get_line(target_line);
    std::size_t col = 0;
    while (col < l.size() && std::isspace(static_cast<unsigned char>(l[col])))
    {
        ++col;
    }
    doc.set_caret(target_line, col, extend_selection);
}

bool search_next(TextDocumentModel& doc, std::string_view query, bool forward, bool extend_selection)
{
    if (query.empty() || doc.get_line_count() == 0) return false;

    const std::size_t total_lines = doc.get_line_count();
    const std::size_t cur_line = doc.get_caret_line();
    const std::size_t cur_col = doc.get_caret_column();

    if (forward)
    {
        // 1. Search remaining part of current line
        std::string_view current_line = doc.get_line(cur_line);
        if (cur_col + 1 < current_line.size())
        {
            const auto pos = current_line.substr(cur_col + 1).find(query);
            if (pos != std::string_view::npos)
            {
                doc.set_caret(cur_line, cur_col + 1 + pos, extend_selection);
                return true;
            }
        }

        // 2. Search lines below
        for (std::size_t l = cur_line + 1; l < total_lines; ++l)
        {
            std::string_view line_text = doc.get_line(l);
            const auto pos = line_text.find(query);
            if (pos != std::string_view::npos)
            {
                doc.set_caret(l, pos, extend_selection);
                return true;
            }
        }

        // 3. Wrap around from line 0 up to current line
        for (std::size_t l = 0; l <= cur_line; ++l)
        {
            std::string_view line_text = doc.get_line(l);
            const auto pos = line_text.find(query);
            if (pos != std::string_view::npos)
            {
                if (l < cur_line || (l == cur_line && pos != cur_col))
                {
                    doc.set_caret(l, pos, extend_selection);
                    return true;
                }
            }
        }
    }
    else
    {
        // Search backwards
        std::string_view current_line = doc.get_line(cur_line);
        if (cur_col > 0)
        {
            const auto pos = current_line.substr(0, cur_col).rfind(query);
            if (pos != std::string_view::npos)
            {
                doc.set_caret(cur_line, pos, extend_selection);
                return true;
            }
        }

        // Search lines above
        for (std::size_t l = cur_line; l-- > 0;)
        {
            std::string_view line_text = doc.get_line(l);
            const auto pos = line_text.rfind(query);
            if (pos != std::string_view::npos)
            {
                doc.set_caret(l, pos, extend_selection);
                return true;
            }
        }

        // Wrap around from bottom
        for (std::size_t l = total_lines; l-- > cur_line;)
        {
            std::string_view line_text = doc.get_line(l);
            const auto pos = line_text.rfind(query);
            if (pos != std::string_view::npos)
            {
                if (l > cur_line || (l == cur_line && pos != cur_col))
                {
                    doc.set_caret(l, pos, extend_selection);
                    return true;
                }
            }
        }
    }

    return false;
}

TextPosition get_WORD_forward_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t total_lines = doc.get_line_count();
    const int steps = std::max(1, count);

    std::size_t cur_line = start.line;
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        std::string_view l = doc.get_line(cur_line);
        // Advance past current non-whitespace
        while (cur_col < l.size() && !std::isspace(static_cast<unsigned char>(l[cur_col])))
        {
            ++cur_col;
        }

        // Advance past whitespace
        while (cur_line < total_lines)
        {
            l = doc.get_line(cur_line);
            while (cur_col < l.size() && std::isspace(static_cast<unsigned char>(l[cur_col])))
            {
                ++cur_col;
            }
            if (cur_col < l.size())
            {
                break;
            }
            if (cur_line + 1 < total_lines)
            {
                ++cur_line;
                cur_col = 0;
            }
            else
            {
                break;
            }
        }
    }

    return TextPosition{cur_line, cur_col};
}

TextPosition get_WORD_end_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t total_lines = doc.get_line_count();
    const int steps = std::max(1, count);

    std::size_t cur_line = start.line;
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        if (cur_col + 1 < doc.get_line(cur_line).size())
        {
            ++cur_col;
        }
        else if (cur_line + 1 < total_lines)
        {
            ++cur_line;
            cur_col = 0;
        }
        else
        {
            break;
        }

        // Advance past whitespace
        while (cur_line < total_lines)
        {
            std::string_view l = doc.get_line(cur_line);
            while (cur_col < l.size() && std::isspace(static_cast<unsigned char>(l[cur_col])))
            {
                ++cur_col;
            }
            if (cur_col < l.size())
            {
                break;
            }
            if (cur_line + 1 < total_lines)
            {
                ++cur_line;
                cur_col = 0;
            }
            else
            {
                break;
            }
        }

        // Advance to end of non-whitespace WORD
        std::string_view l = doc.get_line(cur_line);
        while (cur_col + 1 < l.size() && !std::isspace(static_cast<unsigned char>(l[cur_col + 1])))
        {
            ++cur_col;
        }
    }

    return TextPosition{cur_line, cur_col};
}

TextPosition get_WORD_backward_target(const TextDocumentModel& doc, TextPosition start, int count)
{
    if (doc.get_line_count() == 0) return start;
    const int steps = std::max(1, count);

    std::size_t cur_line = start.line;
    std::size_t cur_col = start.column;

    for (int step = 0; step < steps; ++step)
    {
        if (cur_col == 0)
        {
            if (cur_line > 0)
            {
                --cur_line;
                std::string_view l = doc.get_line(cur_line);
                cur_col = l.empty() ? 0 : l.size() - 1;
            }
            else
            {
                break;
            }
        }
        else
        {
            --cur_col;
        }

        // Skip whitespace backwards
        while (true)
        {
            std::string_view l = doc.get_line(cur_line);
            if (!l.empty() && cur_col < l.size())
            {
                while (cur_col > 0 && std::isspace(static_cast<unsigned char>(l[cur_col])))
                {
                    --cur_col;
                }
                if (!std::isspace(static_cast<unsigned char>(l[cur_col])))
                {
                    break;
                }
            }

            if (cur_line > 0)
            {
                --cur_line;
                l = doc.get_line(cur_line);
                cur_col = l.empty() ? 0 : l.size() - 1;
            }
            else
            {
                cur_col = 0;
                break;
            }
        }

        // Back up to beginning of non-whitespace WORD
        std::string_view l = doc.get_line(cur_line);
        if (!l.empty() && cur_col < l.size())
        {
            while (cur_col > 0 && !std::isspace(static_cast<unsigned char>(l[cur_col - 1])))
            {
                --cur_col;
            }
        }
    }

    return TextPosition{cur_line, cur_col};
}

void move_WORD_forward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_WORD_forward_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_WORD_backward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_WORD_backward_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_WORD_end(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition current{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_WORD_end_target(doc, current, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

TextPosition get_paragraph_target(const TextDocumentModel& doc, TextPosition start, bool forward, int count)
{
    if (doc.get_line_count() == 0) return start;
    const std::size_t total = doc.get_line_count();
    const int steps = std::max(1, count);
    std::size_t line = start.line;

    auto is_blank = [&](std::size_t l) {
        std::string_view text = doc.get_line(l);
        for (char c : text)
        {
            if (!std::isspace(static_cast<unsigned char>(c))) return false;
        }
        return true;
    };

    for (int step = 0; step < steps; ++step)
    {
        if (forward)
        {
            // If already on a blank line, skip blank lines first
            while (line < total && is_blank(line))
            {
                ++line;
            }
            // Advance to next blank line
            while (line < total && !is_blank(line))
            {
                ++line;
            }
            if (line >= total)
            {
                line = total - 1;
                break;
            }
        }
        else
        {
            if (line == 0) break;
            --line;
            // Skip current blank lines
            while (line > 0 && is_blank(line))
            {
                --line;
            }
            // Move up to previous blank line
            while (line > 0 && !is_blank(line))
            {
                --line;
            }
        }
    }

    return TextPosition{line, 0};
}

void move_paragraph_backward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_paragraph_target(doc, cur, false, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

void move_paragraph_forward(TextDocumentModel& doc, int count, bool extend_selection)
{
    const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_paragraph_target(doc, cur, true, count);
    doc.set_caret(target.line, target.column, extend_selection);
}

TextPosition get_find_char_target(const TextDocumentModel& doc, TextPosition start, char target, char find_type, int count)
{
    if (doc.get_line_count() == 0 || start.line >= doc.get_line_count()) return start;
    std::string_view line = doc.get_line(start.line);
    if (line.empty()) return start;

    const int steps = std::max(1, count);
    std::size_t col = start.column;

    if (find_type == 'f' || find_type == 't')
    {
        for (int step = 0; step < steps; ++step)
        {
            const auto pos = line.find(target, col + 1);
            if (pos == std::string_view::npos) return start;
            col = pos;
        }
        if (find_type == 't')
        {
            col = (col > 0) ? (col - 1) : 0;
        }
    }
    else if (find_type == 'F' || find_type == 'T')
    {
        for (int step = 0; step < steps; ++step)
        {
            if (col == 0) return start;
            const auto pos = line.substr(0, col).rfind(target);
            if (pos == std::string_view::npos) return start;
            col = pos;
        }
        if (find_type == 'T' && col + 1 < line.size())
        {
            ++col;
        }
    }

    return TextPosition{start.line, col};
}

bool find_char_on_line(TextDocumentModel& doc, char target, char find_type, int count, bool extend_selection)
{
    const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target_pos = get_find_char_target(doc, cur, target, find_type, count);
    if (target_pos != cur)
    {
        doc.set_caret(target_pos.line, target_pos.column, extend_selection);
        return true;
    }
    return false;
}

bool repeat_find_char(TextDocumentModel& doc, const VimState& state, bool reverse, bool extend_selection)
{
    if (state.last_find_char == '\0' || state.last_find_type == '\0') return false;
    char ft = state.last_find_type;
    if (reverse)
    {
        if (ft == 'f') ft = 'F';
        else if (ft == 'F') ft = 'f';
        else if (ft == 't') ft = 'T';
        else if (ft == 'T') ft = 't';
    }
    return find_char_on_line(doc, state.last_find_char, ft, 1, extend_selection);
}

TextPosition get_matching_bracket_target(const TextDocumentModel& doc, TextPosition start)
{
    if (doc.get_line_count() == 0 || start.line >= doc.get_line_count()) return start;
    std::string_view line = doc.get_line(start.line);
    if (line.empty()) return start;

    auto is_bracket = [](char c) noexcept {
        return c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '<' || c == '>';
    };
    auto get_partner = [](char c) noexcept -> char {
        switch (c)
        {
        case '(': return ')';
        case ')': return '(';
        case '{': return '}';
        case '}': return '{';
        case '[': return ']';
        case ']': return '[';
        case '<': return '>';
        case '>': return '<';
        default: return '\0';
        }
    };
    auto is_open = [](char c) noexcept {
        return c == '(' || c == '{' || c == '[' || c == '<';
    };

    std::size_t col = start.column;
    if (col >= line.size() || !is_bracket(line[col]))
    {
        // Scan forward on line to find first bracket
        while (col < line.size() && !is_bracket(line[col]))
        {
            ++col;
        }
        if (col >= line.size()) return start;
    }

    const char ch = line[col];
    const char partner = get_partner(ch);
    const bool search_forward = is_open(ch);

    int depth = 0;
    const std::size_t total = doc.get_line_count();

    if (search_forward)
    {
        for (std::size_t l = start.line; l < total; ++l)
        {
            std::string_view text = doc.get_line(l);
            const std::size_t s = (l == start.line) ? col : 0;
            for (std::size_t c = s; c < text.size(); ++c)
            {
                if (text[c] == ch) ++depth;
                else if (text[c] == partner)
                {
                    --depth;
                    if (depth == 0) return TextPosition{l, c};
                }
            }
        }
    }
    else
    {
        for (std::size_t l = start.line + 1; l-- > 0;)
        {
            std::string_view text = doc.get_line(l);
            int s = (l == start.line) ? static_cast<int>(col) : static_cast<int>(text.size()) - 1;
            for (int c = s; c >= 0; --c)
            {
                if (text[c] == ch) ++depth;
                else if (text[c] == partner)
                {
                    --depth;
                    if (depth == 0) return TextPosition{l, static_cast<std::size_t>(c)};
                }
            }
        }
    }

    return start;
}

bool move_matching_bracket(TextDocumentModel& doc, bool extend_selection)
{
    const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = get_matching_bracket_target(doc, cur);
    if (target != cur)
    {
        doc.set_caret(target.line, target.column, extend_selection);
        return true;
    }
    return false;
}

bool search_word_under_cursor(TextDocumentModel& doc, VimState& state, bool forward)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    std::string_view l = doc.get_line(line);
    if (col >= l.size() || (!std::isalnum(static_cast<unsigned char>(l[col])) && l[col] != '_'))
    {
        return false;
    }

    std::size_t start = col;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(l[start - 1])) || l[start - 1] == '_'))
    {
        --start;
    }
    std::size_t end = col;
    while (end < l.size() && (std::isalnum(static_cast<unsigned char>(l[end])) || l[end] == '_'))
    {
        ++end;
    }

    state.search_query = std::string(l.substr(start, end - start));
    state.search_forward = forward;
    return search_next(doc, state.search_query, forward, false);
}

std::pair<TextPosition, TextPosition> get_text_object_range(
    const TextDocumentModel& doc, TextPosition pos, char modifier, char obj)
{
    if (doc.get_line_count() == 0 || pos.line >= doc.get_line_count())
    {
        return {pos, pos};
    }

    std::string_view line = doc.get_line(pos.line);

    // 1. Words ('w')
    if (obj == 'w')
    {
        if (line.empty()) return {pos, pos};
        std::size_t start = std::min(pos.column, line.size() - 1);
        const CharClass cls = classify(line[start]);

        // Find start of word
        while (start > 0 && classify(line[start - 1]) == cls)
        {
            --start;
        }

        // Find end of word
        std::size_t end = std::min(pos.column, line.size() - 1);
        while (end + 1 < line.size() && classify(line[end + 1]) == cls)
        {
            ++end;
        }
        if (end < line.size()) ++end; // exclusive end

        // If 'a' (around word), also include trailing whitespace (or leading if at line end)
        if (modifier == 'a')
        {
            if (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])))
            {
                while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])))
                {
                    ++end;
                }
            }
            else if (start > 0 && std::isspace(static_cast<unsigned char>(line[start - 1])))
            {
                while (start > 0 && std::isspace(static_cast<unsigned char>(line[start - 1])))
                {
                    --start;
                }
            }
        }

        return {TextPosition{pos.line, start}, TextPosition{pos.line, end}};
    }

    // 2. Quotes ('"', '\'')
    if (obj == '"' || obj == '\'' || obj == '`')
    {
        const char q = obj;
        if (line.size() < 2) return {pos, pos};

        std::size_t first = std::string_view::npos;
        std::size_t second = std::string_view::npos;

        // Find quote before or at pos, and quote after pos
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == q)
            {
                if (i <= pos.column)
                {
                    first = i;
                }
                else if (first != std::string_view::npos && second == std::string_view::npos)
                {
                    second = i;
                    break;
                }
            }
        }

        if (first != std::string_view::npos && second != std::string_view::npos)
        {
            if (modifier == 'i')
            {
                return {TextPosition{pos.line, first + 1}, TextPosition{pos.line, second}};
            }
            else
            {
                std::size_t start_col = first;
                std::size_t end_col = second + 1;
                if (end_col < line.size() && line[end_col] == ' ')
                {
                    end_col++;
                }
                else if (start_col > 0 && line[start_col - 1] == ' ')
                {
                    start_col--;
                }
                return {TextPosition{pos.line, start_col}, TextPosition{pos.line, end_col}};
            }
        }
    }

    // 3. Brackets ('(', ')', 'b', '{', '}', 'B', '[', ']', '<', '>')
    char open_ch = '(';
    char close_ch = ')';
    if (obj == '{' || obj == '}' || obj == 'B') { open_ch = '{'; close_ch = '}'; }
    else if (obj == '[' || obj == ']') { open_ch = '['; close_ch = ']'; }
    else if (obj == '<' || obj == '>') { open_ch = '<'; close_ch = '>'; }

    // Find opening bracket before pos and closing bracket after pos
    int depth = 0;
    std::optional<TextPosition> open_pos;
    std::optional<TextPosition> close_pos;

    // Search backwards for opening bracket
    for (std::size_t l = pos.line + 1; l-- > 0;)
    {
        std::string_view text = doc.get_line(l);
        int start_col = (l == pos.line) ? static_cast<int>(std::min(pos.column, text.size())) : static_cast<int>(text.size()) - 1;
        for (int c = start_col; c >= 0; --c)
        {
            if (text[c] == close_ch && !(l == pos.line && static_cast<std::size_t>(c) == pos.column))
            {
                ++depth;
            }
            else if (text[c] == open_ch)
            {
                if (depth == 0)
                {
                    open_pos = TextPosition{l, static_cast<std::size_t>(c)};
                    break;
                }
                --depth;
            }
        }
        if (open_pos) break;
    }

    // Search forward from open_pos for closing bracket
    if (open_pos)
    {
        depth = 0;
        for (std::size_t l = open_pos->line; l < doc.get_line_count(); ++l)
        {
            std::string_view text = doc.get_line(l);
            const std::size_t start_col = (l == open_pos->line) ? (open_pos->column + 1) : 0;
            for (std::size_t c = start_col; c < text.size(); ++c)
            {
                if (text[c] == open_ch)
                {
                    ++depth;
                }
                else if (text[c] == close_ch)
                {
                    if (depth == 0)
                    {
                        close_pos = TextPosition{l, c};
                        break;
                    }
                    --depth;
                }
            }
            if (close_pos) break;
        }
    }

    if (open_pos && close_pos)
    {
        if (modifier == 'i')
        {
            TextPosition inner_start = *open_pos;
            ++inner_start.column;
            return {inner_start, *close_pos};
        }
        else
        {
            TextPosition outer_end = *close_pos;
            ++outer_end.column;
            return {*open_pos, outer_end};
        }
    }

    return {pos, pos};
}

} // namespace Zenvra::Editors::VimMotions
