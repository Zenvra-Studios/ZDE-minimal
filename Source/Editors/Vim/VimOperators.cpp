#include "Editors/Vim/VimOperators.h"
#include "Editors/Vim/VimMotions.h"

#include <algorithm>

namespace Zenvra::Editors::VimOperators
{

bool delete_character_under_cursor(TextDocumentModel& doc, VimState& state, int count)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::string_view l = doc.get_line(line);
    if (col >= l.size()) return false;

    const std::size_t n = std::min(static_cast<std::size_t>(std::max(1, count)), l.size() - col);
    const TextPosition start{line, col};
    const TextPosition end{line, col + n};

    state.yank_register = doc.get_text_range(start, end);
    state.yank_is_line = false;

    return doc.delete_range(start, end);
}

bool delete_character_before_cursor(TextDocumentModel& doc, VimState& state, int count)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    if (col == 0) return false;

    const std::size_t n = std::min(static_cast<std::size_t>(std::max(1, count)), col);
    const TextPosition start{line, col - n};
    const TextPosition end{line, col};

    state.yank_register = doc.get_text_range(start, end);
    state.yank_is_line = false;

    return doc.delete_range(start, end);
}

#include <cctype>

static TextPosition resolve_motion_target(
    const TextDocumentModel& doc, TextPosition start, std::string_view motion, int count, bool is_change)
{
    TextPosition target = start;
    if (motion == "w")
    {
        if (is_change)
        {
            target = VimMotions::get_word_end_target(doc, start, count);
            const std::string_view l = doc.get_line(target.line);
            if (target.column < l.size()) ++target.column;
        }
        else
        {
            target = VimMotions::get_word_forward_target(doc, start, count);
        }
    }
    else if (motion == "W")
    {
        if (is_change)
        {
            target = VimMotions::get_WORD_end_target(doc, start, count);
            const std::string_view l = doc.get_line(target.line);
            if (target.column < l.size()) ++target.column;
        }
        else
        {
            target = VimMotions::get_WORD_forward_target(doc, start, count);
        }
    }
    else if (motion == "e")
    {
        target = VimMotions::get_word_end_target(doc, start, count);
        const std::string_view l = doc.get_line(target.line);
        if (target.column < l.size()) ++target.column;
    }
    else if (motion == "E")
    {
        target = VimMotions::get_WORD_end_target(doc, start, count);
        const std::string_view l = doc.get_line(target.line);
        if (target.column < l.size()) ++target.column;
    }
    else if (motion == "b")
    {
        target = VimMotions::get_word_backward_target(doc, start, count);
    }
    else if (motion == "B")
    {
        target = VimMotions::get_WORD_backward_target(doc, start, count);
    }
    else if (motion == "$" || motion == "end")
    {
        target = VimMotions::get_line_end_target(doc, start);
    }
    else if (motion == "0")
    {
        target = TextPosition{start.line, 0};
    }
    else if (motion == "^")
    {
        const std::string_view l = doc.get_line(start.line);
        std::size_t col = 0;
        while (col < l.size() && (l[col] == ' ' || l[col] == '\t')) ++col;
        target = TextPosition{start.line, col};
    }
    else if (motion == "{")
    {
        target = VimMotions::get_paragraph_target(doc, start, false, count);
    }
    else if (motion == "}")
    {
        target = VimMotions::get_paragraph_target(doc, start, true, count);
    }
    else if (motion == "%")
    {
        target = VimMotions::get_matching_bracket_target(doc, start);
        const std::string_view l = doc.get_line(target.line);
        if (target.column < l.size()) ++target.column;
    }
    return target;
}

bool execute_delete_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count)
{
    if (doc.get_line_count() == 0) return false;
    const TextPosition start{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = resolve_motion_target(doc, start, motion, count, false);

    if (start == target) return false;

    TextPosition range_start = start;
    TextPosition range_end = target;
    if (range_end < range_start)
    {
        std::swap(range_start, range_end);
    }

    state.yank_register = doc.get_text_range(range_start, range_end);
    state.yank_is_line = false;

    return doc.delete_range(range_start, range_end);
}

bool execute_change_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count)
{
    if (doc.get_line_count() == 0) return false;
    const TextPosition start{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = resolve_motion_target(doc, start, motion, count, true);

    if (start == target) return false;

    TextPosition range_start = start;
    TextPosition range_end = target;
    if (range_end < range_start)
    {
        std::swap(range_start, range_end);
    }

    state.yank_register = doc.get_text_range(range_start, range_end);
    state.yank_is_line = false;

    doc.delete_range(range_start, range_end);
    state.set_mode(VimMode::Insert);
    return true;
}

bool execute_yank_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count)
{
    if (doc.get_line_count() == 0) return false;
    const TextPosition start{doc.get_caret_line(), doc.get_caret_column()};
    const TextPosition target = resolve_motion_target(doc, start, motion, count, false);

    if (start == target) return false;

    TextPosition range_start = start;
    TextPosition range_end = target;
    if (range_end < range_start)
    {
        std::swap(range_start, range_end);
    }

    state.yank_register = doc.get_text_range(range_start, range_end);
    state.yank_is_line = false;
    return true;
}

bool delete_lines(TextDocumentModel& doc, VimState& state, int count)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t n = static_cast<std::size_t>(std::max(1, count));

    state.yank_register = doc.get_lines_text(line, n);
    state.yank_is_line = true;

    return doc.delete_lines(line, n);
}

bool change_lines(TextDocumentModel& doc, VimState& state, int count)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t n = static_cast<std::size_t>(std::max(1, count));

    state.yank_register = doc.get_lines_text(line, n);
    state.yank_is_line = true;

    if (n > 1)
    {
        doc.delete_lines(line + 1, n - 1);
    }

    // Delete content of the remaining line
    const std::string_view l = doc.get_line(line);
    if (!l.empty())
    {
        doc.delete_range(TextPosition{line, 0}, TextPosition{line, l.size()});
    }

    state.set_mode(VimMode::Insert);
    return true;
}

bool yank_lines(TextDocumentModel& doc, VimState& state, int count)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t n = static_cast<std::size_t>(std::max(1, count));

    state.yank_register = doc.get_lines_text(line, n);
    state.yank_is_line = true;
    return true;
}

bool put_after(TextDocumentModel& doc, VimState& state, int count)
{
    if (state.yank_register.empty() || doc.get_line_count() == 0) return false;
    const int repeats = std::max(1, count);

    doc.record_undo_snapshot();

    if (state.yank_is_line)
    {
        const std::size_t current_line = doc.get_caret_line();
        const std::string_view l = doc.get_line(current_line);
        doc.set_caret(current_line, l.size());

        std::string payload;
        for (int i = 0; i < repeats; ++i)
        {
            payload += '\n';
            std::string_view text = state.yank_register;
            if (!text.empty() && text.back() == '\n')
            {
                text.remove_suffix(1);
            }
            payload += text;
        }

        doc.insert_text(payload);
        doc.set_caret(current_line + 1, 0);
        return true;
    }
    else
    {
        const std::size_t current_line = doc.get_caret_line();
        const std::size_t current_col = doc.get_caret_column();
        const std::string_view l = doc.get_line(current_line);

        const std::size_t insert_col = (current_col < l.size()) ? (current_col + 1) : current_col;
        doc.set_caret(current_line, insert_col);

        std::string payload;
        for (int i = 0; i < repeats; ++i)
        {
            payload += state.yank_register;
        }

        return doc.insert_text(payload);
    }
}

bool put_before(TextDocumentModel& doc, VimState& state, int count)
{
    if (state.yank_register.empty() || doc.get_line_count() == 0) return false;
    const int repeats = std::max(1, count);

    doc.record_undo_snapshot();

    if (state.yank_is_line)
    {
        const std::size_t current_line = doc.get_caret_line();
        doc.set_caret(current_line, 0);

        std::string payload;
        for (int i = 0; i < repeats; ++i)
        {
            std::string_view text = state.yank_register;
            if (!text.empty() && text.back() == '\n')
            {
                text.remove_suffix(1);
            }
            payload += text;
            payload += '\n';
        }

        doc.insert_text(payload);
        doc.set_caret(current_line, 0);
        return true;
    }
    else
    {
        std::string payload;
        for (int i = 0; i < repeats; ++i)
        {
            payload += state.yank_register;
        }

        return doc.insert_text(payload);
    }
}

bool undo(TextDocumentModel& doc)
{
    return doc.undo();
}

bool redo(TextDocumentModel& doc)
{
    return doc.redo();
}

bool execute_text_object_operation(
    TextDocumentModel& doc, VimState& state, char op, char modifier, char obj)
{
    if (doc.get_line_count() == 0) return false;
    const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
    const auto [range_start, range_end] = VimMotions::get_text_object_range(doc, cur, modifier, obj);

    if (range_start == range_end)
    {
        return false;
    }

    state.yank_register = doc.get_text_range(range_start, range_end);
    state.yank_is_line = false;

    if (op == 'd')
    {
        return doc.delete_range(range_start, range_end);
    }
    else if (op == 'c')
    {
        doc.delete_range(range_start, range_end);
        state.set_mode(VimMode::Insert);
        return true;
    }
    else if (op == 'y')
    {
        return true;
    }

    return false;
}

bool execute_find_motion(
    TextDocumentModel& doc, VimState& state, char op, char target, char find_type, int count)
{
    if (doc.get_line_count() == 0) return false;
    const TextPosition start{doc.get_caret_line(), doc.get_caret_column()};
    TextPosition end = VimMotions::get_find_char_target(doc, start, target, find_type, count);
    if (start == end)
    {
        return false;
    }

    if (find_type == 'f' || find_type == 't')
    {
        const std::string_view l = doc.get_line(end.line);
        if (end.column < l.size())
        {
            end.column++;
        }
    }

    TextPosition r_start = start;
    TextPosition r_end = end;
    if (r_end < r_start)
    {
        std::swap(r_start, r_end);
    }

    state.yank_register = doc.get_text_range(r_start, r_end);
    state.yank_is_line = false;

    if (op == 'd')
    {
        return doc.delete_range(r_start, r_end);
    }
    else if (op == 'c')
    {
        doc.delete_range(r_start, r_end);
        state.set_mode(VimMode::Insert);
        return true;
    }
    else if (op == 'y')
    {
        return true;
    }
    return false;
}

bool replace_character(TextDocumentModel& doc, VimState& /*state*/, char new_char)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t line = doc.get_caret_line();
    const std::size_t col = doc.get_caret_column();
    const std::string_view l = doc.get_line(line);
    if (col >= l.size()) return false;

    doc.record_undo_snapshot();
    doc.delete_range(TextPosition{line, col}, TextPosition{line, col + 1});
    doc.set_caret(line, col);
    doc.insert_text(std::string(1, new_char));
    doc.set_caret(line, col);
    return true;
}

bool substitute_character(TextDocumentModel& doc, VimState& state, int count)
{
    if (delete_character_under_cursor(doc, state, count))
    {
        state.set_mode(VimMode::Insert);
        return true;
    }
    return false;
}

bool substitute_line(TextDocumentModel& doc, VimState& state, int count)
{
    return change_lines(doc, state, count);
}

bool join_lines(TextDocumentModel& doc, VimState& /*state*/, int count)
{
    const int repeats = std::max(1, count);
    bool any_joined = false;
    for (int i = 0; i < repeats; ++i)
    {
        const std::size_t line = doc.get_caret_line();
        if (line + 1 >= doc.get_line_count()) break;

        doc.record_undo_snapshot();
        std::string line1{doc.get_line(line)};
        std::string line2{doc.get_line(line + 1)};

        while (!line1.empty() && (line1.back() == ' ' || line1.back() == '\t'))
        {
            line1.pop_back();
        }

        std::size_t l2_start = 0;
        while (l2_start < line2.size() && (line2[l2_start] == ' ' || line2[l2_start] == '\t'))
        {
            ++l2_start;
        }

        const std::size_t join_pos = line1.size();
        std::string joined = line1;
        if (!joined.empty() && l2_start < line2.size())
        {
            joined.push_back(' ');
        }
        if (l2_start < line2.size())
        {
            joined.append(line2.substr(l2_start));
        }

        doc.delete_lines(line + 1, 1);
        const std::string_view cur = doc.get_line(line);
        doc.delete_range(TextPosition{line, 0}, TextPosition{line, cur.size()});
        doc.set_caret(line, 0);
        doc.insert_text(joined);
        doc.set_caret(line, join_pos);
        any_joined = true;
    }
    return any_joined;
}

bool toggle_case_character(TextDocumentModel& doc, VimState& /*state*/, int count)
{
    if (doc.get_line_count() == 0) return false;
    const int repeats = std::max(1, count);
    const std::size_t line = doc.get_caret_line();
    std::size_t col = doc.get_caret_column();
    const std::string_view l = doc.get_line(line);
    if (col >= l.size()) return false;

    doc.record_undo_snapshot();
    for (int i = 0; i < repeats && col < doc.get_line(line).size(); ++i)
    {
        char ch = doc.get_line(line)[col];
        if (std::islower(static_cast<unsigned char>(ch)))
        {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        else if (std::isupper(static_cast<unsigned char>(ch)))
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        doc.delete_range(TextPosition{line, col}, TextPosition{line, col + 1});
        doc.set_caret(line, col);
        doc.insert_text(std::string(1, ch));
        col++;
    }
    const std::string_view updated = doc.get_line(line);
    if (col >= updated.size() && !updated.empty())
    {
        col = updated.size() - 1;
    }
    doc.set_caret(line, col);
    return true;
}

bool indent_lines(TextDocumentModel& doc, VimState& /*state*/, int count, int tab_size)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t start_line = doc.get_caret_line();
    const std::size_t n = std::min(static_cast<std::size_t>(std::max(1, count)), doc.get_line_count() - start_line);
    const std::string indent_str(std::max(1, tab_size), ' ');

    doc.record_undo_snapshot();
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t line = start_line + i;
        doc.set_caret(line, 0);
        doc.insert_text(indent_str);
    }
    doc.set_caret(start_line, 0);
    return true;
}

bool unindent_lines(TextDocumentModel& doc, VimState& /*state*/, int count, int tab_size)
{
    if (doc.get_line_count() == 0) return false;
    const std::size_t start_line = doc.get_caret_line();
    const std::size_t n = std::min(static_cast<std::size_t>(std::max(1, count)), doc.get_line_count() - start_line);
    const std::size_t spaces_to_remove = static_cast<std::size_t>(std::max(1, tab_size));

    doc.record_undo_snapshot();
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t line = start_line + i;
        const std::string_view l = doc.get_line(line);
        std::size_t remove_count = 0;
        while (remove_count < spaces_to_remove && remove_count < l.size() && l[remove_count] == ' ')
        {
            remove_count++;
        }
        if (remove_count == 0 && !l.empty() && l[0] == '\t')
        {
            remove_count = 1;
        }
        if (remove_count > 0)
        {
            doc.delete_range(TextPosition{line, 0}, TextPosition{line, remove_count});
        }
    }
    doc.set_caret(start_line, 0);
    return true;
}

} // namespace Zenvra::Editors::VimOperators
