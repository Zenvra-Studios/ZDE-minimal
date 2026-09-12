#include "Editors/Vim/VimEditorMode.h"
#include "Editors/Vim/VimMotions.h"
#include "Editors/Vim/VimOperators.h"
#include "Editors/Vim/VimVisualMode.h"
#include "UI/Editor/EditorController.h"

#include <cctype>

namespace Zenvra::Editors
{

VimEditorMode::VimEditorMode()
{
    m_state.mode = VimMode::Normal;
}

void VimEditorMode::activate()
{
    m_state.set_mode(VimMode::Normal);
    m_sequence.clear();
}

void VimEditorMode::deactivate()
{
    m_state.reset_pending();
    m_sequence.clear();
}

std::string_view VimEditorMode::get_mode_name() const noexcept
{
    if (m_state.mode == VimMode::Command)
    {
        m_mode_display = ":" + m_state.command_buffer;
        return m_mode_display;
    }
    if (m_state.search_active)
    {
        m_mode_display = (m_state.search_forward ? "/" : "?") + m_state.search_query;
        return m_mode_display;
    }
    return vim_mode_to_string(m_state.mode);
}

std::string_view VimEditorMode::get_cursor_shape() const noexcept
{
    if (m_state.mode == VimMode::Insert)
    {
        return "Line";
    }
    return "Block";
}

bool VimEditorMode::is_insert_mode() const noexcept
{
    return m_state.mode == VimMode::Insert;
}

void VimEditorMode::set_mode(VimMode mode) noexcept
{
    m_state.set_mode(mode);
}

void VimEditorMode::update(double delta_time_sec)
{
    if (m_sequence.update_and_check_timeout(delta_time_sec, m_state.timeout_seconds))
    {
        m_state.reset_pending();
    }
}

bool VimEditorMode::handle_text_input(
    std::string_view utf8_text,
    TextDocumentModel& doc,
    EditorController& controller)
{
    if (m_state.mode == VimMode::Insert)
    {
        return false;
    }

    for (std::size_t i = 0; i < utf8_text.size(); ++i)
    {
        EditorKeyEvent ev;
        ev.character = static_cast<char32_t>(static_cast<unsigned char>(utf8_text[i]));
        ev.text = std::string(1, utf8_text[i]);
        handle_key(ev, doc, controller);
    }
    return true;
}

bool VimEditorMode::handle_key(
    const EditorKeyEvent& event,
    TextDocumentModel& doc,
    EditorController& controller)
{
    // 1. In Insert mode: Escape returns to Normal mode, all other keys pass to editor
    if (m_state.mode == VimMode::Insert)
    {
        if (event.key_code == 27 || event.character == 27 ||
            (!m_state.escape_key.empty() && m_state.escape_key != "Escape" && event.text == m_state.escape_key))
        {
            if (doc.get_caret_column() > 0)
            {
                doc.set_caret(doc.get_caret_line(), doc.get_caret_column() - 1);
            }
            m_state.set_mode(VimMode::Normal);
            return true;
        }
        return false;
    }

    // 2. In Visual / VisualLine mode
    if (m_state.mode == VimMode::Visual || m_state.mode == VimMode::VisualLine)
    {
        if (event.key_code == 27 || event.character == 27 ||
            (!m_state.escape_key.empty() && m_state.escape_key != "Escape" && event.text == m_state.escape_key))
        {
            VimVisualMode::exit_visual(doc, m_state);
            return true;
        }

        char c = static_cast<char>(event.character);
        if (c == '\0' && !event.text.empty())
        {
            c = event.text.front();
        }

        if (c != '\0')
        {
            // Number prefix for visual motion
            if (std::isdigit(static_cast<unsigned char>(c)) && (c != '0' || m_state.count > 0))
            {
                m_state.count = m_state.count * 10 + (c - '0');
                return true;
            }

            return VimVisualMode::handle_visual_key(doc, m_state, c);
        }
        return false;
    }

    // 3. In Command mode (Ex mode, e.g. after typing ':')
    if (m_state.mode == VimMode::Command)
    {
        if (event.key_code == 27 || event.character == 27)
        {
            m_state.command_buffer.clear();
            m_state.set_mode(VimMode::Normal);
            return true;
        }

        char c = static_cast<char>(event.character);
        if (c == '\0' && !event.text.empty())
        {
            c = event.text.front();
        }

        if (c != '\0')
        {
            return handle_command_key(c, doc, controller);
        }
        return false;
    }

    // 4. In Normal mode
    char c = static_cast<char>(event.character);
    if (c == '\0' && !event.text.empty())
    {
        c = event.text.front();
    }
    if (c == '\0' && event.key_code == 27)
    {
        c = '\x1b';
    }

    // Ctrl key shortcuts in Normal and Visual modes
    if (event.ctrl)
    {
        if (event.key_code == 'R' || event.key_code == 'r' || c == 18)
        {
            m_state.reset_pending();
            return VimOperators::redo(doc);
        }
        if (event.key_code == 'D' || event.key_code == 'd' || c == 4)
        {
            m_state.reset_pending();
            VimMotions::move_down(doc, 15, m_state.mode == VimMode::Visual || m_state.mode == VimMode::VisualLine);
            return true;
        }
        if (event.key_code == 'U' || event.key_code == 'u' || c == 21)
        {
            m_state.reset_pending();
            VimMotions::move_up(doc, 15, m_state.mode == VimMode::Visual || m_state.mode == VimMode::VisualLine);
            return true;
        }
        if (event.key_code == 'F' || event.key_code == 'f' || c == 6)
        {
            m_state.reset_pending();
            VimMotions::move_down(doc, 30, m_state.mode == VimMode::Visual || m_state.mode == VimMode::VisualLine);
            return true;
        }
        if (event.key_code == 'B' || event.key_code == 'b' || c == 2)
        {
            m_state.reset_pending();
            VimMotions::move_up(doc, 30, m_state.mode == VimMode::Visual || m_state.mode == VimMode::VisualLine);
            return true;
        }
    }

    if (c != '\0')
    {
        return handle_normal_key(c, event, doc, controller);
    }

    return false;
}

bool VimEditorMode::handle_normal_key(char c, const EditorKeyEvent& /*event*/, TextDocumentModel& doc, EditorController& /*controller*/)
{
    // Search input accumulation
    if (m_state.search_active)
    {
        if (c == '\r' || c == '\n')
        {
            m_state.search_active = false;
            return VimMotions::search_next(doc, m_state.search_query, m_state.search_forward);
        }
        if (c == '\x1b')
        {
            m_state.search_active = false;
            m_state.search_query.clear();
            return true;
        }
        if (c == '\b')
        {
            if (!m_state.search_query.empty())
            {
                m_state.search_query.pop_back();
            }
            return true;
        }
        m_state.search_query.push_back(c);
        return true;
    }

    // Escape in Normal mode clears pending state and selection
    if (c == '\x1b')
    {
        m_state.reset_pending();
        m_sequence.clear();
        doc.clear_selection();
        return true;
    }

    // 1. Pending single character replace ('r')
    if (m_state.pending_replace)
    {
        m_state.pending_replace = false;
        return VimOperators::replace_character(doc, m_state, c);
    }

    // 2. Pending in-line character find ('f', 'F', 't', 'T')
    if (m_state.pending_find)
    {
        m_state.pending_find = false;
        const int count = m_state.get_effective_count();
        if (m_state.pending_operator != '\0')
        {
            const char op = m_state.pending_operator;
            m_state.pending_operator = '\0';
            m_state.last_find_char = c;
            const bool ok = VimOperators::execute_find_motion(doc, m_state, op, c, m_state.last_find_type, count);
            m_state.clear_count();
            return ok;
        }
        else
        {
            m_state.last_find_char = c;
            VimMotions::find_char_on_line(doc, c, m_state.last_find_type, count);
            m_state.clear_count();
            return true;
        }
    }

    // 3. Pending text object modifier (e.g. 'di' or 'ca')
    if (m_state.pending_text_object_modifier != '\0')
    {
        const char modifier = m_state.pending_text_object_modifier;
        const char op = m_state.pending_operator;
        m_state.pending_text_object_modifier = '\0';
        m_state.pending_operator = '\0';
        m_state.clear_count();
        return VimOperators::execute_text_object_operation(doc, m_state, op, modifier, c);
    }

    // Accumulate numeric prefix count (e.g. 5j, 3w, 2dd)
    if (std::isdigit(static_cast<unsigned char>(c)) && (c != '0' || m_state.count > 0))
    {
        m_state.count = m_state.count * 10 + (c - '0');
        return true;
    }

    const int count = m_state.get_effective_count();

    // Handle operator pending (e.g. d, c, y already pressed)
    if (m_state.pending_operator != '\0')
    {
        // Text objects trigger: diw, daw, di", da", di(, etc.
        if (c == 'i' || c == 'a')
        {
            m_state.pending_text_object_modifier = c;
            return true;
        }

        // In-line find motion trigger: dfx, dt", etc.
        if (c == 'f' || c == 'F' || c == 't' || c == 'T')
        {
            m_state.pending_find = true;
            m_state.last_find_type = c;
            return true;
        }

        const char op = m_state.pending_operator;
        m_state.pending_operator = '\0';

        // Repeated operator: dd, cc, yy
        if (c == op)
        {
            m_state.clear_count();
            if (op == 'd') return VimOperators::delete_lines(doc, m_state, count);
            if (op == 'c') return VimOperators::change_lines(doc, m_state, count);
            if (op == 'y') return VimOperators::yank_lines(doc, m_state, count);
        }

        // Motion combinations: dw, dW, de, dE, db, dB, d$, d0, d^, d{, d}, d%
        std::string motion_str(1, c);
        m_state.clear_count();

        if (op == 'd')
        {
            return VimOperators::execute_delete_motion(doc, m_state, motion_str, count);
        }
        if (op == 'c')
        {
            return VimOperators::execute_change_motion(doc, m_state, motion_str, count);
        }
        if (op == 'y')
        {
            return VimOperators::execute_yank_motion(doc, m_state, motion_str, count);
        }

        return true;
    }

    // Handle multi-key sequences starting with 'g'
    if (m_sequence.get() == "g")
    {
        m_sequence.clear();
        if (c == 'g')
        {
            VimMotions::move_document_start(doc, m_state.count);
            m_state.clear_count();
            return true;
        }
        m_state.reset_pending();
        return true;
    }

    // Multi-key sequences for indent / unindent ('>>', '<<')
    if (c == '>')
    {
        if (m_sequence.get() == ">")
        {
            m_sequence.clear();
            VimOperators::indent_lines(doc, m_state, count, m_state.tab_size);
            m_state.clear_count();
            return true;
        }
        m_sequence.push('>');
        return true;
    }
    if (c == '<')
    {
        if (m_sequence.get() == "<")
        {
            m_sequence.clear();
            VimOperators::unindent_lines(doc, m_state, count, m_state.tab_size);
            m_state.clear_count();
            return true;
        }
        m_sequence.push('<');
        return true;
    }

    // Normal mode commands
    switch (c)
    {
    // Basic motions
    case 'h':
        VimMotions::move_left(doc, count);
        m_state.clear_count();
        return true;
    case 'j':
        VimMotions::move_down(doc, count);
        m_state.clear_count();
        return true;
    case 'k':
        VimMotions::move_up(doc, count);
        m_state.clear_count();
        return true;
    case 'l':
    case ' ':
        VimMotions::move_right(doc, count);
        m_state.clear_count();
        return true;
    case '\r':
    case '\n':
        VimMotions::move_down(doc, count);
        VimMotions::move_first_non_blank(doc);
        m_state.clear_count();
        return true;
    case '\b':
    case 127:
        VimMotions::move_left(doc, count);
        m_state.clear_count();
        return true;
    case '\t':
        return true;

    // Word motions
    case 'w':
        VimMotions::move_word_forward(doc, count);
        m_state.clear_count();
        return true;
    case 'b':
        VimMotions::move_word_backward(doc, count);
        m_state.clear_count();
        return true;
    case 'e':
        VimMotions::move_word_end(doc, count);
        m_state.clear_count();
        return true;

    // Big WORD motions
    case 'W':
        VimMotions::move_WORD_forward(doc, count);
        m_state.clear_count();
        return true;
    case 'B':
        VimMotions::move_WORD_backward(doc, count);
        m_state.clear_count();
        return true;
    case 'E':
        VimMotions::move_WORD_end(doc, count);
        m_state.clear_count();
        return true;

    // Line motions
    case '0':
        VimMotions::move_line_start(doc);
        m_state.clear_count();
        return true;
    case '^':
        VimMotions::move_first_non_blank(doc);
        m_state.clear_count();
        return true;
    case '$':
        VimMotions::move_line_end(doc);
        m_state.clear_count();
        return true;

    // Document motions
    case 'G':
        VimMotions::move_document_end(doc, m_state.count);
        m_state.clear_count();
        return true;
    case 'g':
        m_sequence.push('g');
        return true;

    // Paragraph motions
    case '{':
        VimMotions::move_paragraph_backward(doc, count);
        m_state.clear_count();
        return true;
    case '}':
        VimMotions::move_paragraph_forward(doc, count);
        m_state.clear_count();
        return true;

    // Matching bracket motion (%)
    case '%':
        VimMotions::move_matching_bracket(doc);
        m_state.clear_count();
        return true;

    // In-line find character motions
    case 'f':
    case 'F':
    case 't':
    case 'T':
        m_state.pending_find = true;
        m_state.last_find_type = c;
        return true;
    case ';':
        VimMotions::repeat_find_char(doc, m_state, false);
        m_state.clear_count();
        return true;
    case ',':
        VimMotions::repeat_find_char(doc, m_state, true);
        m_state.clear_count();
        return true;

    // Word search under cursor (*, #)
    case '*':
        VimMotions::search_word_under_cursor(doc, m_state, true);
        return true;
    case '#':
        VimMotions::search_word_under_cursor(doc, m_state, false);
        return true;

    // Insert mode entry
    case 'i':
        m_state.set_mode(VimMode::Insert);
        return true;
    case 'I':
        VimMotions::move_first_non_blank(doc);
        m_state.set_mode(VimMode::Insert);
        return true;
    case 'a':
        if (doc.get_caret_column() < doc.get_line(doc.get_caret_line()).size())
        {
            doc.set_caret(doc.get_caret_line(), doc.get_caret_column() + 1);
        }
        m_state.set_mode(VimMode::Insert);
        return true;
    case 'A':
        doc.set_caret(doc.get_caret_line(), doc.get_line(doc.get_caret_line()).size());
        m_state.set_mode(VimMode::Insert);
        return true;
    case 'o':
    {
        const std::size_t cur = doc.get_caret_line();
        doc.set_caret(cur, doc.get_line(cur).size());
        doc.execute(UI::Editor::EditorInputCommand::InsertNewLine, false);
        m_state.set_mode(VimMode::Insert);
        return true;
    }
    case 'O':
    {
        const std::size_t cur = doc.get_caret_line();
        doc.set_caret(cur, 0);
        doc.insert_text("\n");
        doc.set_caret(cur, 0);
        m_state.set_mode(VimMode::Insert);
        return true;
    }

    // Operators & modifications
    case 'd':
        m_state.pending_operator = 'd';
        return true;
    case 'D':
        VimOperators::execute_delete_motion(doc, m_state, "$", count);
        m_state.clear_count();
        return true;
    case 'c':
        m_state.pending_operator = 'c';
        return true;
    case 'C':
        VimOperators::execute_change_motion(doc, m_state, "$", count);
        m_state.clear_count();
        return true;
    case 'y':
        m_state.pending_operator = 'y';
        return true;
    case 'x':
        VimOperators::delete_character_under_cursor(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'X':
        VimOperators::delete_character_before_cursor(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'r':
        m_state.pending_replace = true;
        return true;
    case 's':
        VimOperators::substitute_character(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'S':
        VimOperators::substitute_line(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'J':
        VimOperators::join_lines(doc, m_state, count);
        m_state.clear_count();
        return true;
    case '~':
        VimOperators::toggle_case_character(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'p':
        VimOperators::put_after(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'P':
        VimOperators::put_before(doc, m_state, count);
        m_state.clear_count();
        return true;
    case 'u':
        m_state.clear_count();
        return VimOperators::undo(doc);

    // Visual mode entry
    case 'v':
        VimVisualMode::enter_visual(doc, m_state);
        return true;
    case 'V':
        VimVisualMode::enter_visual_line(doc, m_state);
        return true;

    // Search
    case '/':
        m_state.search_active = true;
        m_state.search_forward = true;
        m_state.search_query.clear();
        return true;
    case '?':
        m_state.search_active = true;
        m_state.search_forward = false;
        m_state.search_query.clear();
        return true;
    case 'n':
        VimMotions::search_next(doc, m_state.search_query, m_state.search_forward);
        return true;
    case 'N':
        VimMotions::search_next(doc, m_state.search_query, !m_state.search_forward);
        return true;

    // Command-line mode entry (':')
    case ':':
        m_state.set_mode(VimMode::Command);
        m_state.command_buffer.clear();
        return true;

    default:
        break;
    }

    return false;
}

bool VimEditorMode::handle_command_key(char c, TextDocumentModel& doc, EditorController& controller)
{
    if (c == '\r' || c == '\n')
    {
        std::string cmd = m_state.command_buffer;
        m_state.command_buffer.clear();
        m_state.set_mode(VimMode::Normal);
        return execute_command_line(cmd, doc, controller);
    }
    if (c == '\x1b')
    {
        m_state.command_buffer.clear();
        m_state.set_mode(VimMode::Normal);
        return true;
    }
    if (c == '\b')
    {
        if (!m_state.command_buffer.empty())
        {
            m_state.command_buffer.pop_back();
        }
        else
        {
            m_state.set_mode(VimMode::Normal);
        }
        return true;
    }

    m_state.command_buffer.push_back(c);
    return true;
}

bool VimEditorMode::execute_command_line(
    std::string_view cmd_view,
    TextDocumentModel& doc,
    EditorController& controller)
{
    // Trim whitespace
    while (!cmd_view.empty() && std::isspace(static_cast<unsigned char>(cmd_view.front())))
    {
        cmd_view.remove_prefix(1);
    }
    while (!cmd_view.empty() && std::isspace(static_cast<unsigned char>(cmd_view.back())))
    {
        cmd_view.remove_suffix(1);
    }

    if (cmd_view.empty()) return true;

    // 1. Line number jump: :<number>
    bool all_digits = true;
    for (char ch : cmd_view)
    {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
        {
            all_digits = false;
            break;
        }
    }
    if (all_digits)
    {
        try
        {
            std::size_t line_num = std::stoull(std::string(cmd_view));
            if (line_num > 0) line_num--;
            if (line_num >= doc.get_line_count())
            {
                line_num = doc.get_line_count() > 0 ? doc.get_line_count() - 1 : 0;
            }
            doc.set_caret(line_num, 0);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // 2. Save (:w, :write, :w!)
    if (cmd_view == "w" || cmd_view == "w!" || cmd_view == "write")
    {
        doc.mark_saved();
        return controller.execute_action(UI::Editor::EditorAction::SaveDocument);
    }

    // 3. Quit (:q, :q!, :quit)
    if (cmd_view == "q" || cmd_view == "q!" || cmd_view == "quit")
    {
        return controller.execute_action(UI::Editor::EditorAction::CloseDocument);
    }

    // 4. Save and Quit (:wq, :wq!, :x, :x!)
    if (cmd_view == "wq" || cmd_view == "wq!" || cmd_view == "x" || cmd_view == "x!")
    {
        doc.mark_saved();
        static_cast<void>(controller.execute_action(UI::Editor::EditorAction::SaveDocument));
        return controller.execute_action(UI::Editor::EditorAction::CloseDocument);
    }

    // 5. Save all (:wa, :wall)
    if (cmd_view == "wa" || cmd_view == "wall")
    {
        doc.mark_saved();
        return controller.execute_action(UI::Editor::EditorAction::SaveDocument);
    }

    // 6. Quit all (:qa, :qa!, :qall)
    if (cmd_view == "qa" || cmd_view == "qa!" || cmd_view == "qall")
    {
        return controller.close_all_files();
    }

    // 7. Clear search highlight (:noh, :nohl, :nohlsearch)
    if (cmd_view == "noh" || cmd_view == "nohl" || cmd_view == "nohlsearch")
    {
        m_state.search_query.clear();
        return true;
    }

    // 8. Open / edit (:e <filename>, :edit <filename>)
    if (cmd_view.starts_with("e ") || cmd_view.starts_with("edit "))
    {
        std::size_t space_idx = cmd_view.find(' ');
        std::string filename{cmd_view.substr(space_idx + 1)};
        while (!filename.empty() && std::isspace(static_cast<unsigned char>(filename.front())))
        {
            filename.erase(filename.begin());
        }
        if (!filename.empty())
        {
            return controller.open_file(filename);
        }
        return false;
    }

    // 9. Substitution: :s/find/replace/ or :%s/find/replace/g
    const bool is_document_range = cmd_view.starts_with("%s");
    const bool is_line_sub = cmd_view.starts_with("s");
    if (is_document_range || is_line_sub)
    {
        std::size_t offset = is_document_range ? 2 : 1;
        if (offset < cmd_view.size())
        {
            char delim = cmd_view[offset];
            if (delim == '/' || delim == '#')
            {
                std::size_t first = offset;
                std::size_t second = cmd_view.find(delim, first + 1);
                if (second != std::string_view::npos)
                {
                    std::string_view find_str = cmd_view.substr(first + 1, second - (first + 1));
                    std::size_t third = cmd_view.find(delim, second + 1);
                    std::string_view replace_str;
                    bool global_replace = false;
                    if (third != std::string_view::npos)
                    {
                        replace_str = cmd_view.substr(second + 1, third - (second + 1));
                        std::string_view flags = cmd_view.substr(third + 1);
                        if (flags.find('g') != std::string_view::npos)
                        {
                            global_replace = true;
                        }
                    }
                    else
                    {
                        replace_str = cmd_view.substr(second + 1);
                    }

                    if (!find_str.empty())
                    {
                        doc.record_undo_snapshot();
                        const std::size_t start_line = is_document_range ? 0 : doc.get_caret_line();
                        const std::size_t end_line = is_document_range ? doc.get_line_count() : (doc.get_caret_line() + 1);

                        for (std::size_t l = start_line; l < end_line && l < doc.get_line_count(); ++l)
                        {
                            std::string line{doc.get_line(l)};
                            std::size_t pos = 0;
                            bool line_modified = false;
                            while ((pos = line.find(find_str, pos)) != std::string::npos)
                            {
                                line.replace(pos, find_str.size(), replace_str);
                                pos += replace_str.size();
                                line_modified = true;
                                if (!global_replace) break;
                            }
                            if (line_modified)
                            {
                                const std::string_view cur = doc.get_line(l);
                                doc.delete_range(TextPosition{l, 0}, TextPosition{l, cur.size()});
                                doc.set_caret(l, 0);
                                doc.insert_text(line);
                            }
                        }
                        return true;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace Zenvra::Editors
