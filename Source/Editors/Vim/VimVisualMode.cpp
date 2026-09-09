#include "Editors/Vim/VimVisualMode.h"
#include "Editors/Vim/VimMotions.h"
#include "Editors/Vim/VimOperators.h"

#include <cctype>

namespace Zenvra::Editors::VimVisualMode
{

namespace VimOperators = Zenvra::Editors::VimOperators;

void enter_visual(TextDocumentModel& doc, VimState& state)
{
    state.visual_anchor = TextPosition{doc.get_caret_line(), doc.get_caret_column()};
    state.set_mode(VimMode::Visual);
    // Start character selection on current position
    doc.set_caret(state.visual_anchor.line, state.visual_anchor.column, true);
}

void enter_visual_line(TextDocumentModel& doc, VimState& state)
{
    state.visual_anchor = TextPosition{doc.get_caret_line(), 0};
    state.set_mode(VimMode::VisualLine);
    doc.select_line_at(doc.get_caret_line());
}

void exit_visual(TextDocumentModel& doc, VimState& state)
{
    doc.clear_selection();
    state.set_mode(VimMode::Normal);
}

bool handle_visual_key(TextDocumentModel& doc, VimState& state, char key)
{
    if (state.mode != VimMode::Visual && state.mode != VimMode::VisualLine)
    {
        return false;
    }

    // 1. Pending find character motion
    if (state.pending_find)
    {
        VimMotions::find_char_on_line(doc, key, state.last_find_type, state.get_effective_count(), true);
        state.last_find_char = key;
        state.pending_find = false;
        state.clear_count();
        return true;
    }

    // 2. Pending replace character for visual selection
    if (state.pending_replace)
    {
        state.pending_replace = false;
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            std::string text = doc.get_selected_text();
            for (char& c : text)
            {
                if (c != '\n' && c != '\r')
                {
                    c = key;
                }
            }
            doc.delete_selection();
            doc.insert_text(text);
            doc.set_caret(sel.start.line, sel.start.column);
        }
        state.set_mode(VimMode::Normal);
        return true;
    }

    // 3. Pending text object modifier ('i' or 'a')
    if (state.pending_text_object_modifier != '\0')
    {
        const char modifier = state.pending_text_object_modifier;
        state.pending_text_object_modifier = '\0';
        state.clear_count();

        const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
        const auto [start, end] = VimMotions::get_text_object_range(doc, cur, modifier, key);
        if (start < end)
        {
            doc.set_caret(start.line, start.column, false);
            doc.set_caret(end.line, end.column, true);
            state.visual_anchor = start;
        }
        return true;
    }

    // Handle text object trigger ('i' or 'a')
    if (key == 'i' || key == 'a')
    {
        state.pending_text_object_modifier = key;
        return true;
    }

    const int count = state.get_effective_count();

    switch (key)
    {
    case 'h':
        VimMotions::move_left(doc, count, true);
        state.clear_count();
        return true;
    case 'l':
        VimMotions::move_right(doc, count, true);
        state.clear_count();
        return true;
    case 'j':
        VimMotions::move_down(doc, count, true);
        state.clear_count();
        return true;
    case 'k':
        VimMotions::move_up(doc, count, true);
        state.clear_count();
        return true;
    case 'w':
        VimMotions::move_word_forward(doc, count, true);
        state.clear_count();
        return true;
    case 'b':
        VimMotions::move_word_backward(doc, count, true);
        state.clear_count();
        return true;
    case 'e':
        VimMotions::move_word_end(doc, count, true);
        state.clear_count();
        return true;
    case 'W':
        VimMotions::move_WORD_forward(doc, count, true);
        state.clear_count();
        return true;
    case 'B':
        VimMotions::move_WORD_backward(doc, count, true);
        state.clear_count();
        return true;
    case 'E':
        VimMotions::move_WORD_end(doc, count, true);
        state.clear_count();
        return true;
    case '{':
        VimMotions::move_paragraph_backward(doc, count, true);
        state.clear_count();
        return true;
    case '}':
        VimMotions::move_paragraph_forward(doc, count, true);
        state.clear_count();
        return true;
    case '%':
        VimMotions::move_matching_bracket(doc, true);
        state.clear_count();
        return true;
    case 'G':
        VimMotions::move_document_end(doc, state.count, true);
        state.clear_count();
        return true;
    case '0':
        VimMotions::move_line_start(doc, true);
        state.clear_count();
        return true;
    case '^':
        VimMotions::move_first_non_blank(doc, true);
        state.clear_count();
        return true;
    case '$':
        VimMotions::move_line_end(doc, true);
        state.clear_count();
        return true;

    // In-line find motions
    case 'f':
    case 'F':
    case 't':
    case 'T':
        state.pending_find = true;
        state.last_find_type = key;
        return true;
    case ';':
        VimMotions::repeat_find_char(doc, state, false, true);
        state.clear_count();
        return true;
    case ',':
        VimMotions::repeat_find_char(doc, state, true, true);
        state.clear_count();
        return true;

    // Switch selection ends (anchor <-> caret)
    case 'o':
    case 'O':
    {
        const TextPosition cur{doc.get_caret_line(), doc.get_caret_column()};
        const TextPosition old_anchor = state.visual_anchor;
        state.visual_anchor = cur;
        doc.set_caret(cur.line, cur.column, false);
        doc.set_caret(old_anchor.line, old_anchor.column, true);
        return true;
    }

    // Search word under cursor
    case '*':
        VimMotions::search_word_under_cursor(doc, state, true);
        return true;
    case '#':
        VimMotions::search_word_under_cursor(doc, state, false);
        return true;

    // Command line mode entry
    case ':':
        state.set_mode(VimMode::Command);
        state.command_buffer.clear();
        return true;

    // Single character replace for selection
    case 'r':
        state.pending_replace = true;
        return true;

    // Case conversions
    case '~':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            std::string text = doc.get_selected_text();
            for (char& c : text)
            {
                if (std::islower(static_cast<unsigned char>(c)))
                {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                else if (std::isupper(static_cast<unsigned char>(c)))
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            doc.delete_selection();
            doc.insert_text(text);
            doc.set_caret(sel.start.line, sel.start.column);
        }
        state.set_mode(VimMode::Normal);
        return true;
    }
    case 'u':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            std::string text = doc.get_selected_text();
            for (char& c : text)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            doc.delete_selection();
            doc.insert_text(text);
            doc.set_caret(sel.start.line, sel.start.column);
        }
        state.set_mode(VimMode::Normal);
        return true;
    }
    case 'U':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            std::string text = doc.get_selected_text();
            for (char& c : text)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            doc.delete_selection();
            doc.insert_text(text);
            doc.set_caret(sel.start.line, sel.start.column);
        }
        state.set_mode(VimMode::Normal);
        return true;
    }

    // Indent / Unindent selection
    case '>':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            const std::size_t start_line = sel.start.line;
            const std::size_t end_line = sel.end.line;
            const std::string indent_str(std::max(1, state.tab_size), ' ');
            doc.record_undo_snapshot();
            for (std::size_t l = start_line; l <= end_line && l < doc.get_line_count(); ++l)
            {
                doc.set_caret(l, 0);
                doc.insert_text(indent_str);
            }
            doc.set_caret(start_line, 0);
        }
        exit_visual(doc, state);
        return true;
    }
    case '<':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            const std::size_t start_line = sel.start.line;
            const std::size_t end_line = sel.end.line;
            const std::size_t spaces_to_remove = static_cast<std::size_t>(std::max(1, state.tab_size));
            doc.record_undo_snapshot();
            for (std::size_t l = start_line; l <= end_line && l < doc.get_line_count(); ++l)
            {
                const std::string_view line_str = doc.get_line(l);
                std::size_t rem = 0;
                while (rem < spaces_to_remove && rem < line_str.size() && line_str[rem] == ' ') rem++;
                if (rem == 0 && !line_str.empty() && line_str[0] == '\t') rem = 1;
                if (rem > 0)
                {
                    doc.delete_range(TextPosition{l, 0}, TextPosition{l, rem});
                }
            }
            doc.set_caret(start_line, 0);
        }
        exit_visual(doc, state);
        return true;
    }

    // Join lines in selection
    case 'J':
    {
        if (doc.has_selection())
        {
            const auto sel = doc.get_selection();
            const int lines_to_join = static_cast<int>(sel.end.line - sel.start.line);
            doc.set_caret(sel.start.line, 0);
            exit_visual(doc, state);
            VimOperators::join_lines(doc, state, lines_to_join);
        }
        return true;
    }

    case 'd':
    case 'x':
        if (doc.has_selection())
        {
            state.yank_register = doc.get_selected_text();
            state.yank_is_line = (state.mode == VimMode::VisualLine);
            doc.delete_selection();
        }
        state.set_mode(VimMode::Normal);
        return true;

    case 'y':
        if (doc.has_selection())
        {
            state.yank_register = doc.get_selected_text();
            state.yank_is_line = (state.mode == VimMode::VisualLine);
            doc.clear_selection();
        }
        state.set_mode(VimMode::Normal);
        return true;

    case 'c':
        if (doc.has_selection())
        {
            state.yank_register = doc.get_selected_text();
            state.yank_is_line = (state.mode == VimMode::VisualLine);
            doc.delete_selection();
        }
        state.set_mode(VimMode::Insert);
        return true;

    case '\x1b': // ESC
        exit_visual(doc, state);
        return true;

    default:
        break;
    }

    return false;
}

} // namespace Zenvra::Editors::VimVisualMode
