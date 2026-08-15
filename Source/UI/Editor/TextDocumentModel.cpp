#include "UI/Editor/TextDocumentModel.h"

#include "UI/Editor/CppSymbolLexer.h"
#include "Language/LanguageConfiguration.h"
#include <algorithm>
#include <filesystem>

namespace Zenvra::UI::Editor
{

namespace
{

bool is_utf8_continuation(char character)
{
    return (static_cast<unsigned char>(character) & 0xC0U) == 0x80U;
}

std::size_t previous_character_column(std::string_view line, std::size_t column)
{
    if (column == 0)
    {
        return 0;
    }
    --column;
    while (column > 0 && is_utf8_continuation(line[column]))
    {
        --column;
    }
    return column;
}

std::size_t next_character_column(std::string_view line, std::size_t column)
{
    if (column >= line.size())
    {
        return line.size();
    }
    ++column;
    while (column < line.size() && is_utf8_continuation(line[column]))
    {
        ++column;
    }
    return column;
}

std::size_t clamp_to_character_boundary(std::string_view line, std::size_t column)
{
    column = std::min(column, line.size());
    while (column > 0 && column < line.size() && is_utf8_continuation(line[column]))
    {
        --column;
    }
    return column;
}

std::size_t character_count(std::string_view text)
{
    return static_cast<std::size_t>(std::count_if(
        text.begin(), text.end(), [](char character) {
            return !is_utf8_continuation(character);
        }));
}

bool is_word_character(char character)
{
    const unsigned char value = static_cast<unsigned char>(character);
    if (value >= 0x80U)
    {
        return true;
    }
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') ||
        value == '_';
}

} // namespace

TextDocumentModel::TextDocumentModel()
{
    m_lines.emplace_back();
    m_selection_anchor = {m_caret_line, m_caret_column};
    update_preferred_column();
}

void TextDocumentModel::replace_contents(
    std::vector<std::string> lines,
    std::string file_name,
    std::vector<BreadcrumbItem> breadcrumbs,
    std::string line_ending,
    bool read_only)
{
    if (lines.empty())
    {
        lines.emplace_back();
    }
    m_lines = std::move(lines);
    m_file_name = std::move(file_name);
    m_breadcrumbs = std::move(breadcrumbs);
    m_line_ending = std::move(line_ending);
    m_read_only = read_only;
    m_caret_line = 0;
    m_caret_column = 0;
    m_selection_anchor = {};
    m_secondary_cursors.clear();
    m_dirty = false;
    update_preferred_column();
}

void TextDocumentModel::update_file_identity(
    std::string file_name,
    std::vector<BreadcrumbItem> breadcrumbs,
    std::string line_ending)
{
    m_file_name = std::move(file_name);
    m_breadcrumbs = std::move(breadcrumbs);
    m_line_ending = std::move(line_ending);
}

std::size_t TextDocumentModel::get_line_count() const noexcept
{
    return m_lines.size();
}

std::string_view TextDocumentModel::get_line(std::size_t line_index) const noexcept
{
    return line_index < m_lines.size() ? std::string_view{m_lines[line_index]} : std::string_view{};
}

std::size_t TextDocumentModel::get_caret_line() const noexcept
{
    return m_caret_line;
}

std::size_t TextDocumentModel::get_caret_column() const noexcept
{
    return m_caret_column;
}

std::string_view TextDocumentModel::get_file_name() const noexcept
{
    return m_file_name;
}

std::span<const BreadcrumbItem> TextDocumentModel::get_breadcrumbs() const noexcept
{
    return m_breadcrumbs;
}

std::vector<BreadcrumbItem> TextDocumentModel::get_full_breadcrumbs() const
{
    std::vector<BreadcrumbItem> result = m_breadcrumbs;

    // Determine if this is a C++ file by checking the file extension.
    const std::filesystem::path file_path{m_file_name};
    const std::string ext = file_path.extension().string();
    const bool is_cpp = ext == ".cpp" || ext == ".cxx" || ext == ".cc" ||
        ext == ".h" || ext == ".hpp" || ext == ".hxx" || ext == ".hh" ||
        ext == ".c" || ext == ".inl";

    if (is_cpp && !m_lines.empty())
    {
        std::vector<BreadcrumbItem> symbol_scopes =
            CppSymbolLexer::resolve_scopes(m_lines, m_caret_line);
        result.insert(result.end(), symbol_scopes.begin(), symbol_scopes.end());
    }

    return result;
}

FooterEditorStatus TextDocumentModel::get_status() const noexcept
{
    return FooterEditorStatus{
        .line = m_caret_line + 1,
        .column = character_count(get_line(m_caret_line).substr(0, m_caret_column)) + 1,
        .line_ending = m_line_ending,
        .encoding = "UTF-8",
        .indent_width = 4,
    };
}

bool TextDocumentModel::is_dirty() const noexcept
{
    return m_dirty;
}

bool TextDocumentModel::is_read_only() const noexcept
{
    return m_read_only;
}

bool TextDocumentModel::has_selection() const noexcept
{
    return m_selection_anchor != TextPosition{m_caret_line, m_caret_column};
}

TextSelection TextDocumentModel::get_selection() const noexcept
{
    const TextPosition caret{m_caret_line, m_caret_column};
    return m_selection_anchor <= caret
        ? TextSelection{m_selection_anchor, caret}
        : TextSelection{caret, m_selection_anchor};
}

std::string TextDocumentModel::get_selected_text() const
{
    if (!has_selection())
    {
        return {};
    }
    const TextSelection selection = get_selection();
    if (selection.start.line == selection.end.line)
    {
        return m_lines[selection.start.line].substr(
            selection.start.column,
            selection.end.column - selection.start.column);
    }

    std::string result = m_lines[selection.start.line].substr(selection.start.column);
    result.push_back('\n');
    for (std::size_t line = selection.start.line + 1; line < selection.end.line; ++line)
    {
        result += m_lines[line];
        result.push_back('\n');
    }
    result += m_lines[selection.end.line].substr(0, selection.end.column);
    return result;
}

std::span<const std::string> TextDocumentModel::get_lines() const noexcept
{
    return m_lines;
}

std::vector<TextCursor> TextDocumentModel::get_all_cursors() const
{
    std::vector<TextCursor> result;
    result.reserve(1 + m_secondary_cursors.size());
    result.push_back(TextCursor{
        .line = m_caret_line,
        .column = m_caret_column,
        .preferred_column = m_preferred_column,
        .selection_anchor = m_selection_anchor,
    });
    for (const auto& cursor : m_secondary_cursors)
    {
        result.push_back(cursor);
    }
    return result;
}

bool TextDocumentModel::has_secondary_cursors() const noexcept
{
    return !m_secondary_cursors.empty();
}

bool TextDocumentModel::set_caret(
    std::size_t line_index,
    std::size_t byte_column,
    bool extend_selection) noexcept
{
    const bool had_selection = has_selection();
    const TextPosition clamped = clamped_position({line_index, byte_column});
    line_index = clamped.line;
    byte_column = clamped.column;
    const bool changed = line_index != m_caret_line || byte_column != m_caret_column;
    m_caret_line = line_index;
    m_caret_column = byte_column;
    if (!extend_selection)
    {
        m_selection_anchor = {m_caret_line, m_caret_column};
        m_secondary_cursors.clear();
    }
    update_preferred_column();
    return changed || (!extend_selection && had_selection);
}

bool TextDocumentModel::select_all() noexcept
{
    m_secondary_cursors.clear();
    const TextPosition previous_caret{m_caret_line, m_caret_column};
    const TextPosition previous_anchor = m_selection_anchor;
    m_selection_anchor = {};
    m_caret_line = m_lines.size() - 1;
    m_caret_column = m_lines.back().size();
    update_preferred_column();
    return previous_anchor != m_selection_anchor ||
        previous_caret != TextPosition{m_caret_line, m_caret_column};
}

bool TextDocumentModel::select_word_at(std::size_t line_index, std::size_t byte_column)
{
    m_secondary_cursors.clear();
    if (m_read_only || line_index >= m_lines.size())
    {
        return false;
    }
    const std::string& line = m_lines[line_index];
    if (line.empty())
    {
        return false;
    }
    byte_column = clamp_to_character_boundary(line, byte_column);
    if (byte_column >= line.size())
    {
        byte_column = line.size() == 0 ? 0 : previous_character_column(line, line.size());
    }
    const bool clicked_word = is_word_character(line[byte_column]);
    std::size_t start = byte_column;
    while (start > 0)
    {
        const std::size_t previous = previous_character_column(line, start);
        if (is_word_character(line[previous]) != clicked_word)
        {
            break;
        }
        start = previous;
    }
    std::size_t end = byte_column;
    while (end < line.size())
    {
        const std::size_t next = next_character_column(line, end);
        if (is_word_character(line[end]) != clicked_word)
        {
            break;
        }
        end = next;
    }
    if (start == end)
    {
        return false;
    }
    m_selection_anchor = {line_index, start};
    m_caret_line = line_index;
    m_caret_column = end;
    update_preferred_column();
    return true;
}

bool TextDocumentModel::select_line_at(std::size_t line_index) noexcept
{
    m_secondary_cursors.clear();
    if (m_read_only || line_index >= m_lines.size())
    {
        return false;
    }
    const TextPosition previous_caret{m_caret_line, m_caret_column};
    const TextPosition previous_anchor = m_selection_anchor;
    m_caret_line = line_index;
    m_caret_column = m_lines[line_index].size();
    m_selection_anchor = {line_index, 0};
    update_preferred_column();
    return previous_anchor != m_selection_anchor ||
        previous_caret != TextPosition{m_caret_line, m_caret_column};
}

bool TextDocumentModel::clear_selection() noexcept
{
    m_secondary_cursors.clear();
    if (!has_selection())
    {
        return false;
    }
    m_selection_anchor = {m_caret_line, m_caret_column};
    return true;
}

bool TextDocumentModel::insert_text(std::string_view utf8_text)
{
    if (m_read_only || utf8_text.empty())
    {
        return false;
    }

    if (!m_secondary_cursors.empty())
    {
        std::vector<TextCursor> all = get_all_cursors();
        std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
            if (a.line != b.line) return a.line > b.line;
            return a.column > b.column;
        });

        for (auto& cur : all)
        {
            if (cur.has_selection())
            {
                const TextSelection sel = cur.get_selection();
                if (sel.start.line == sel.end.line && sel.start.line == cur.line)
                {
                    m_lines[cur.line].erase(sel.start.column, sel.end.column - sel.start.column);
                    cur.column = sel.start.column;
                }
            }
            m_lines[cur.line].insert(cur.column, utf8_text);
            cur.column += utf8_text.size();
            cur.selection_anchor = {cur.line, cur.column};
            cur.preferred_column = cur.column;
        }

        std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
            if (a.line != b.line) return a.line < b.line;
            return a.column < b.column;
        });

        all.erase(std::unique(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
            return a.line == b.line && a.column == b.column;
        }), all.end());

        m_caret_line = all.front().line;
        m_caret_column = all.front().column;
        m_preferred_column = all.front().preferred_column;
        m_selection_anchor = all.front().selection_anchor;

        m_secondary_cursors.clear();
        for (std::size_t i = 1; i < all.size(); ++i)
        {
            m_secondary_cursors.push_back(all[i]);
        }

        m_dirty = true;
        return true;
    }

    bool changed = delete_selection();
    std::size_t segment_start = 0;
    
    // Auto closing braces
    std::string text_to_insert{utf8_text};
    bool auto_close_brace = false;
    if (text_to_insert == "{")
    {
        std::size_t dot_pos = m_file_name.find_last_of('.');
        if (dot_pos != std::string::npos)
        {
            std::string ext = m_file_name.substr(dot_pos);
            if (ext == ".cpp" || ext == ".h" || ext == ".c" || ext == ".hpp")
            {
                text_to_insert = "{}";
                auto_close_brace = true;
            }
        }
    }
    
    for (std::size_t index = 0; index <= text_to_insert.size(); ++index)
    {
        const bool at_end = index == text_to_insert.size();
        const bool at_line_break = !at_end && (text_to_insert[index] == '\n' || text_to_insert[index] == '\r');
        if (!at_end && !at_line_break)
        {
            continue;
        }
        if (index > segment_start)
        {
            const std::string_view segment = std::string_view{text_to_insert}.substr(segment_start, index - segment_start);
            m_lines[m_caret_line].insert(m_caret_column, segment);
            m_caret_column += segment.size();
            changed = true;
        }
        if (at_line_break)
        {
            std::string remainder = m_lines[m_caret_line].substr(m_caret_column);
            m_lines[m_caret_line].erase(m_caret_column);
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), remainder);
            ++m_caret_line;
            m_caret_column = 0;
            changed = true;
            if (text_to_insert[index] == '\r' && index + 1 < text_to_insert.size() &&
                text_to_insert[index + 1] == '\n')
            {
                ++index;
            }
            segment_start = index + 1;
        }
    }
    
    if (auto_close_brace && changed)
    {
        if (m_caret_column > 0)
        {
            m_caret_column -= 1;
        }
    }
    
    if (changed)
    {
        m_dirty = true;
        m_selection_anchor = {m_caret_line, m_caret_column};
        update_preferred_column();
    }
    return changed;
}

bool TextDocumentModel::execute(EditorInputCommand command, bool extend_selection)
{
    if (command == EditorInputCommand::AddCursorAbove)
    {
        return add_cursor_above();
    }
    if (command == EditorInputCommand::AddCursorBelow)
    {
        return add_cursor_below();
    }
    if (command == EditorInputCommand::Escape)
    {
        return clear_secondary_cursors();
    }

    if (!m_secondary_cursors.empty())
    {
        if (command == EditorInputCommand::DeleteBackward)
        {
            if (m_read_only) return false;
            std::vector<TextCursor> all = get_all_cursors();
            std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                if (a.line != b.line) return a.line > b.line;
                return a.column > b.column;
            });
            for (auto& cur : all)
            {
                if (cur.has_selection())
                {
                    const TextSelection sel = cur.get_selection();
                    if (sel.start.line == sel.end.line && sel.start.line == cur.line)
                    {
                        m_lines[cur.line].erase(sel.start.column, sel.end.column - sel.start.column);
                        cur.column = sel.start.column;
                        cur.selection_anchor = {cur.line, cur.column};
                        cur.preferred_column = cur.column;
                    }
                }
                else if (cur.column > 0)
                {
                    const std::size_t erase_start = previous_character_column(m_lines[cur.line], cur.column);
                    m_lines[cur.line].erase(erase_start, cur.column - erase_start);
                    cur.column = erase_start;
                    cur.selection_anchor = {cur.line, cur.column};
                    cur.preferred_column = cur.column;
                }
            }
            std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                if (a.line != b.line) return a.line < b.line;
                return a.column < b.column;
            });
            all.erase(std::unique(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                return a.line == b.line && a.column == b.column;
            }), all.end());
            m_caret_line = all.front().line;
            m_caret_column = all.front().column;
            m_preferred_column = all.front().preferred_column;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            m_dirty = true;
            return true;
        }
        if (command == EditorInputCommand::DeleteForward)
        {
            if (m_read_only) return false;
            std::vector<TextCursor> all = get_all_cursors();
            std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                if (a.line != b.line) return a.line > b.line;
                return a.column > b.column;
            });
            for (auto& cur : all)
            {
                if (cur.has_selection())
                {
                    const TextSelection sel = cur.get_selection();
                    if (sel.start.line == sel.end.line && sel.start.line == cur.line)
                    {
                        m_lines[cur.line].erase(sel.start.column, sel.end.column - sel.start.column);
                        cur.column = sel.start.column;
                        cur.selection_anchor = {cur.line, cur.column};
                        cur.preferred_column = cur.column;
                    }
                }
                else if (cur.column < m_lines[cur.line].size())
                {
                    const std::size_t next_col = next_character_column(m_lines[cur.line], cur.column);
                    m_lines[cur.line].erase(cur.column, next_col - cur.column);
                    cur.selection_anchor = {cur.line, cur.column};
                    cur.preferred_column = cur.column;
                }
            }
            std::sort(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                if (a.line != b.line) return a.line < b.line;
                return a.column < b.column;
            });
            all.erase(std::unique(all.begin(), all.end(), [](const TextCursor& a, const TextCursor& b) {
                return a.line == b.line && a.column == b.column;
            }), all.end());
            m_caret_line = all.front().line;
            m_caret_column = all.front().column;
            m_preferred_column = all.front().preferred_column;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            m_dirty = true;
            return true;
        }
        if (command == EditorInputCommand::MoveLeft)
        {
            std::vector<TextCursor> all = get_all_cursors();
            for (auto& cur : all)
            {
                if (cur.column > 0)
                {
                    cur.column = previous_character_column(m_lines[cur.line], cur.column);
                }
                cur.preferred_column = cur.column;
                if (!extend_selection) cur.selection_anchor = {cur.line, cur.column};
            }
            m_caret_line = all.front().line;
            m_caret_column = all.front().column;
            m_preferred_column = all.front().preferred_column;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            return true;
        }
        if (command == EditorInputCommand::MoveRight)
        {
            std::vector<TextCursor> all = get_all_cursors();
            for (auto& cur : all)
            {
                if (cur.column < m_lines[cur.line].size())
                {
                    cur.column = next_character_column(m_lines[cur.line], cur.column);
                }
                cur.preferred_column = cur.column;
                if (!extend_selection) cur.selection_anchor = {cur.line, cur.column};
            }
            m_caret_line = all.front().line;
            m_caret_column = all.front().column;
            m_preferred_column = all.front().preferred_column;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            return true;
        }
        if (command == EditorInputCommand::MoveHome)
        {
            std::vector<TextCursor> all = get_all_cursors();
            for (auto& cur : all)
            {
                cur.column = 0;
                cur.preferred_column = 0;
                if (!extend_selection) cur.selection_anchor = {cur.line, 0};
            }
            m_caret_line = all.front().line;
            m_caret_column = 0;
            m_preferred_column = 0;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            return true;
        }
        if (command == EditorInputCommand::MoveEnd)
        {
            std::vector<TextCursor> all = get_all_cursors();
            for (auto& cur : all)
            {
                cur.column = m_lines[cur.line].size();
                cur.preferred_column = cur.column;
                if (!extend_selection) cur.selection_anchor = {cur.line, cur.column};
            }
            m_caret_line = all.front().line;
            m_caret_column = all.front().column;
            m_preferred_column = all.front().preferred_column;
            m_selection_anchor = all.front().selection_anchor;
            m_secondary_cursors.clear();
            for (std::size_t i = 1; i < all.size(); ++i)
            {
                m_secondary_cursors.push_back(all[i]);
            }
            return true;
        }
        if (command == EditorInputCommand::MoveUp || command == EditorInputCommand::MoveDown)
        {
            clear_secondary_cursors();
        }
    }

    const bool editing_command = command == EditorInputCommand::InsertNewLine ||
        command == EditorInputCommand::InsertTab ||
        command == EditorInputCommand::DeleteBackward ||
        command == EditorInputCommand::DeleteForward ||
        command == EditorInputCommand::MoveLineUp ||
        command == EditorInputCommand::MoveLineDown;
    if (m_read_only && editing_command)
    {
        return false;
    }
    const std::size_t original_line = m_caret_line;
    const std::size_t original_column = m_caret_column;
    const bool originally_selected = has_selection();
    if (!extend_selection && originally_selected &&
        (command == EditorInputCommand::MoveLeft || command == EditorInputCommand::MoveRight))
    {
        const TextSelection selection = get_selection();
        const TextPosition target = command == EditorInputCommand::MoveLeft
            ? selection.start
            : selection.end;
        m_caret_line = target.line;
        m_caret_column = target.column;
        m_selection_anchor = target;
        update_preferred_column();
        return true;
    }

    bool edited = false;
    switch (command)
    {
    case EditorInputCommand::MoveLeft:
        if (m_caret_column > 0)
        {
            m_caret_column = previous_character_column(m_lines[m_caret_line], m_caret_column);
        }
        else if (m_caret_line > 0)
        {
            --m_caret_line;
            m_caret_column = m_lines[m_caret_line].size();
        }
        update_preferred_column();
        break;
    case EditorInputCommand::MoveRight:
        if (m_caret_column < m_lines[m_caret_line].size())
        {
            m_caret_column = next_character_column(m_lines[m_caret_line], m_caret_column);
        }
        else if (m_caret_line + 1 < m_lines.size())
        {
            ++m_caret_line;
            m_caret_column = 0;
        }
        update_preferred_column();
        break;
    case EditorInputCommand::MoveUp:
        if (m_caret_line > 0)
        {
            --m_caret_line;
            m_caret_column = clamp_to_character_boundary(
                m_lines[m_caret_line], m_preferred_column);
        }
        break;
    case EditorInputCommand::MoveDown:
        if (m_caret_line + 1 < m_lines.size())
        {
            ++m_caret_line;
            m_caret_column = clamp_to_character_boundary(
                m_lines[m_caret_line], m_preferred_column);
        }
        break;
    case EditorInputCommand::MoveHome:
        m_caret_column = 0;
        update_preferred_column();
        break;
    case EditorInputCommand::MoveEnd:
        m_caret_column = m_lines[m_caret_line].size();
        update_preferred_column();
        break;
    case EditorInputCommand::InsertNewLine:
        edited = delete_selection() || edited;
        insert_new_line();
        edited = true;
        break;
    case EditorInputCommand::InsertTab:
        edited = delete_selection() || edited;
        {
            const std::size_t spaces_to_add = 4 - (m_caret_column % 4);
            m_lines[m_caret_line].insert(m_caret_column, spaces_to_add, ' ');
            m_caret_column += spaces_to_add;
        }
        edited = true;
        break;
    case EditorInputCommand::DeleteBackward:
        if (has_selection())
        {
            edited = delete_selection();
        }
        else if (m_caret_column > 0 || m_caret_line > 0)
        {
            delete_backward();
            edited = true;
        }
        break;
    case EditorInputCommand::DeleteForward:
        if (has_selection())
        {
            edited = delete_selection();
        }
        else if (m_caret_column < m_lines[m_caret_line].size() ||
                 m_caret_line + 1 < m_lines.size())
        {
            delete_forward();
            edited = true;
        }
        break;
    case EditorInputCommand::MoveLineUp:
        return move_line_up();
    case EditorInputCommand::MoveLineDown:
        return move_line_down();
    }
    if (edited)
    {
        m_dirty = true;
        m_selection_anchor = {m_caret_line, m_caret_column};
        update_preferred_column();
    }
    else
    {
        begin_or_clear_selection(
            extend_selection, TextPosition{original_line, original_column});
    }
    return edited || m_caret_line != original_line || m_caret_column != original_column;
}

bool TextDocumentModel::delete_selection()
{
    if (m_read_only || !has_selection() || m_lines.empty())
    {
        return false;
    }
    const TextSelection selection = get_selection();
    if (selection.start.line >= m_lines.size() || selection.end.line >= m_lines.size())
    {
        return false;
    }
    if (selection.start.line == selection.end.line)
    {
        const std::size_t col_start = std::min(selection.start.column, m_lines[selection.start.line].size());
        const std::size_t col_end = std::min(selection.end.column, m_lines[selection.start.line].size());
        if (col_start < col_end)
        {
            m_lines[selection.start.line].erase(col_start, col_end - col_start);
        }
    }
    else
    {
        const std::size_t col_start = std::min(selection.start.column, m_lines[selection.start.line].size());
        m_lines[selection.start.line].erase(col_start);
        const std::size_t col_end = std::min(selection.end.column, m_lines[selection.end.line].size());
        m_lines[selection.start.line] += m_lines[selection.end.line].substr(col_end);
        
        const auto erase_first = m_lines.begin() + static_cast<std::ptrdiff_t>(selection.start.line + 1);
        const auto erase_last = m_lines.begin() + static_cast<std::ptrdiff_t>(selection.end.line + 1);
        if (erase_first < erase_last && erase_last <= m_lines.end())
        {
            m_lines.erase(erase_first, erase_last);
        }
    }
    m_caret_line = selection.start.line;
    m_caret_column = selection.start.column;
    m_selection_anchor = selection.start;
    m_dirty = true;
    update_preferred_column();
    return true;
}

bool TextDocumentModel::toggle_line_comment()
{
    if (m_read_only)
    {
        return false;
    }
    const TextSelection selection = has_selection() ? get_selection() : TextSelection{{m_caret_line, 0}, {m_caret_line, m_lines[m_caret_line].size()}};
    
    std::string ext = "";
    std::size_t dot_pos = m_file_name.find_last_of('.');
    if (dot_pos != std::string::npos)
    {
        ext = m_file_name.substr(dot_pos);
    }
    
    const Language::LanguageConfiguration config = Language::LanguageConfiguration::get_for_extension(ext);
    const std::string comment_str = config.line_comment;
    
    bool all_commented = true;
    std::size_t min_indent = std::string::npos;
    
    for (std::size_t i = selection.start.line; i <= selection.end.line; ++i)
    {
        const std::string& line = m_lines[i];
        std::size_t first_non_ws = line.find_first_not_of(" \t");
        if (first_non_ws == std::string::npos) continue;
        
        if (first_non_ws < min_indent) min_indent = first_non_ws;
        
        if (first_non_ws + comment_str.size() > line.size() || line.substr(first_non_ws, comment_str.size()) != comment_str)
        {
            all_commented = false;
        }
    }
    
    if (min_indent == std::string::npos) return false;
    
    for (std::size_t i = selection.start.line; i <= selection.end.line; ++i)
    {
        std::string& line = m_lines[i];
        std::size_t first_non_ws = line.find_first_not_of(" \t");
        if (first_non_ws == std::string::npos) continue;
        
        if (all_commented)
        {
            std::size_t erase_len = comment_str.size();
            if (first_non_ws + erase_len < line.size() && line[first_non_ws + erase_len] == ' ')
            {
                erase_len++;
            }
            line.erase(first_non_ws, erase_len);
        }
        else
        {
            line.insert(min_indent, comment_str + " ");
        }
    }
    
    m_dirty = true;
    update_preferred_column();
    return true;
}

bool TextDocumentModel::move_line_up()
{
    if (m_read_only || m_lines.empty())
    {
        return false;
    }

    std::size_t start_line = m_caret_line;
    std::size_t end_line = m_caret_line;
    if (has_selection())
    {
        const TextSelection selection = get_selection();
        start_line = selection.start.line;
        end_line = (selection.end.column == 0 && selection.end.line > selection.start.line)
            ? selection.end.line - 1
            : selection.end.line;
    }

    if (start_line == 0 || end_line >= m_lines.size() || start_line > end_line)
    {
        return false;
    }

    const auto first = m_lines.begin() + static_cast<std::ptrdiff_t>(start_line - 1);
    const auto middle = m_lines.begin() + static_cast<std::ptrdiff_t>(start_line);
    const auto last = m_lines.begin() + static_cast<std::ptrdiff_t>(end_line + 1);
    if (first < middle && middle < last && last <= m_lines.end())
    {
        std::rotate(first, middle, last);
    }

    --m_caret_line;
    if (has_selection())
    {
        --m_selection_anchor.line;
        m_selection_anchor.column = clamp_to_character_boundary(
            m_lines[m_selection_anchor.line], m_selection_anchor.column);
    }
    else
    {
        m_selection_anchor = {m_caret_line, m_caret_column};
    }
    m_caret_column = clamp_to_character_boundary(m_lines[m_caret_line], m_caret_column);

    m_dirty = true;
    update_preferred_column();
    return true;
}

bool TextDocumentModel::move_line_down()
{
    if (m_read_only || m_lines.empty())
    {
        return false;
    }

    std::size_t start_line = m_caret_line;
    std::size_t end_line = m_caret_line;
    if (has_selection())
    {
        const TextSelection selection = get_selection();
        start_line = selection.start.line;
        end_line = (selection.end.column == 0 && selection.end.line > selection.start.line)
            ? selection.end.line - 1
            : selection.end.line;
    }

    if (end_line + 1 >= m_lines.size() || start_line > end_line)
    {
        return false;
    }

    const auto first = m_lines.begin() + static_cast<std::ptrdiff_t>(start_line);
    const auto middle = m_lines.begin() + static_cast<std::ptrdiff_t>(end_line + 1);
    const auto last = m_lines.begin() + static_cast<std::ptrdiff_t>(end_line + 2);
    if (first < middle && middle < last && last <= m_lines.end())
    {
        std::rotate(first, middle, last);
    }

    ++m_caret_line;
    if (has_selection())
    {
        ++m_selection_anchor.line;
        m_selection_anchor.column = clamp_to_character_boundary(
            m_lines[m_selection_anchor.line], m_selection_anchor.column);
    }
    else
    {
        m_selection_anchor = {m_caret_line, m_caret_column};
    }
    m_caret_column = clamp_to_character_boundary(m_lines[m_caret_line], m_caret_column);

    m_dirty = true;
    update_preferred_column();
    return true;
}

bool TextDocumentModel::add_cursor_above()
{
    if (m_lines.empty())
    {
        return false;
    }

    std::size_t min_line = m_caret_line;
    std::size_t preferred_col = m_preferred_column;
    for (const auto& cur : m_secondary_cursors)
    {
        if (cur.line < min_line)
        {
            min_line = cur.line;
            preferred_col = cur.preferred_column;
        }
    }

    if (min_line == 0)
    {
        return false;
    }

    const std::size_t target_line = min_line - 1;
    const std::size_t target_col = clamp_to_character_boundary(
        m_lines[target_line], preferred_col);

    if (m_caret_line == target_line && m_caret_column == target_col)
    {
        return false;
    }
    for (const auto& cur : m_secondary_cursors)
    {
        if (cur.line == target_line && cur.column == target_col)
        {
            return false;
        }
    }

    m_secondary_cursors.push_back(TextCursor{
        .line = target_line,
        .column = target_col,
        .preferred_column = preferred_col,
        .selection_anchor = {target_line, target_col},
    });

    return true;
}

bool TextDocumentModel::add_cursor_below()
{
    if (m_lines.empty())
    {
        return false;
    }

    std::size_t max_line = m_caret_line;
    std::size_t preferred_col = m_preferred_column;
    for (const auto& cur : m_secondary_cursors)
    {
        if (cur.line > max_line)
        {
            max_line = cur.line;
            preferred_col = cur.preferred_column;
        }
    }

    if (max_line + 1 >= m_lines.size())
    {
        return false;
    }

    const std::size_t target_line = max_line + 1;
    const std::size_t target_col = clamp_to_character_boundary(
        m_lines[target_line], preferred_col);

    if (m_caret_line == target_line && m_caret_column == target_col)
    {
        return false;
    }
    for (const auto& cur : m_secondary_cursors)
    {
        if (cur.line == target_line && cur.column == target_col)
        {
            return false;
        }
    }

    m_secondary_cursors.push_back(TextCursor{
        .line = target_line,
        .column = target_col,
        .preferred_column = preferred_col,
        .selection_anchor = {target_line, target_col},
    });

    return true;
}

bool TextDocumentModel::clear_secondary_cursors() noexcept
{
    if (!m_secondary_cursors.empty())
    {
        m_secondary_cursors.clear();
        return true;
    }
    return false;
}

void TextDocumentModel::mark_saved() noexcept
{
    m_dirty = false;
}

void TextDocumentModel::insert_new_line()
{
    std::string previous_line_content = m_lines[m_caret_line].substr(0, m_caret_column);
    std::string remainder = m_lines[m_caret_line].substr(m_caret_column);
    
    std::string auto_indent;
    std::size_t first_non_ws = previous_line_content.find_first_not_of(" \t");
    if (first_non_ws != std::string::npos)
    {
        auto_indent = previous_line_content.substr(0, first_non_ws);
    }
    else
    {
        auto_indent = previous_line_content;
    }

    bool is_block_comment_start = false;
    bool is_block_comment_middle = false;
    bool is_open_brace = false;

    if (first_non_ws != std::string::npos)
    {
        std::string trimmed_prev = previous_line_content.substr(first_non_ws);
        if (trimmed_prev.ends_with("/**") || trimmed_prev == "/**" ||
            trimmed_prev.ends_with("/***") || trimmed_prev == "/***")
        {
            is_block_comment_start = true;
        }
        else if (trimmed_prev.starts_with("* "))
        {
            is_block_comment_middle = true;
        }
        else if (trimmed_prev.ends_with("{") || trimmed_prev.ends_with("(") || trimmed_prev.ends_with("["))
        {
            is_open_brace = true;
        }
    }

    m_lines[m_caret_line].erase(m_caret_column);
    
    if (is_block_comment_start)
    {
        m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), auto_indent + " * ");
        if (remainder.empty() || remainder == "*/" || remainder == "**/" || remainder == "***/")
        {
            if (remainder.empty())
            {
                m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 2), auto_indent + " **/");
            }
            else
            {
                if (remainder == "*/")
                {
                    remainder = "**/";
                }
                else if (remainder == "**/")
                {
                    remainder = "***/";
                }
                m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 2), auto_indent + " " + remainder);
            }
        }
        else
        {
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 2), auto_indent + " " + remainder);
        }
        ++m_caret_line;
        m_caret_column = auto_indent.size() + 3;
    }
    else if (is_block_comment_middle)
    {
        m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), auto_indent + "* " + remainder);
        ++m_caret_line;
        m_caret_column = auto_indent.size() + 2;
    }
    else if (is_open_brace)
    {
        std::string extra_indent = "    ";
        if (remainder.starts_with("}") || remainder == "}" || remainder.find('}') != std::string::npos)
        {
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), auto_indent + extra_indent);
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 2), auto_indent + remainder);
            ++m_caret_line;
            m_caret_column = auto_indent.size() + extra_indent.size();
        }
        else
        {
            m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), auto_indent + extra_indent + remainder);
            ++m_caret_line;
            m_caret_column = auto_indent.size() + extra_indent.size();
        }
    }
    else
    {
        m_lines.insert(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1), auto_indent + remainder);
        ++m_caret_line;
        m_caret_column = auto_indent.size();
    }
}

void TextDocumentModel::delete_backward()
{
    if (m_lines.empty() || m_caret_line >= m_lines.size())
    {
        return;
    }
    if (m_caret_column > 0)
    {
        const std::string& line = m_lines[m_caret_line];

        // Check if caret is between auto-closing pairs: (), [], {}, "", ''
        if (m_caret_column < line.size())
        {
            const char prev_char = line[m_caret_column - 1];
            const char next_char = line[m_caret_column];
            if ((prev_char == '(' && next_char == ')') ||
                (prev_char == '[' && next_char == ']') ||
                (prev_char == '{' && next_char == '}') ||
                (prev_char == '"' && next_char == '"') ||
                (prev_char == '\'' && next_char == '\''))
            {
                m_lines[m_caret_line].erase(m_caret_column - 1, 2);
                --m_caret_column;
                return;
            }
        }

        constexpr std::size_t kIndentWidth = 4;
        std::size_t erase_start = previous_character_column(line, m_caret_column);
        if (line.find_first_not_of(' ') >= m_caret_column)
        {
            std::size_t width = m_caret_column % kIndentWidth;
            if (width == 0)
            {
                width = kIndentWidth;
            }
            erase_start = m_caret_column - width;
        }
        erase_start = std::min(erase_start, line.size());
        const std::size_t erase_len = (m_caret_column > erase_start) ? (m_caret_column - erase_start) : 0;
        m_lines[m_caret_line].erase(erase_start, erase_len);
        m_caret_column = erase_start;
        return;
    }
    if (m_caret_line > 0 && m_caret_line < m_lines.size())
    {
        const std::size_t previous_line = m_caret_line - 1;
        const std::size_t previous_length = m_lines[previous_line].size();
        m_lines[previous_line] += m_lines[m_caret_line];
        m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line));
        m_caret_line = previous_line;
        m_caret_column = previous_length;
    }
}

void TextDocumentModel::delete_forward()
{
    if (m_lines.empty() || m_caret_line >= m_lines.size())
    {
        return;
    }
    if (m_caret_column < m_lines[m_caret_line].size())
    {
        const std::size_t next_column = next_character_column(
            m_lines[m_caret_line], m_caret_column);
        m_lines[m_caret_line].erase(m_caret_column, next_column - m_caret_column);
        return;
    }
    if (m_caret_line + 1 < m_lines.size())
    {
        m_lines[m_caret_line] += m_lines[m_caret_line + 1];
        m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(m_caret_line + 1));
    }
}

void TextDocumentModel::update_preferred_column() noexcept
{
    m_preferred_column = m_caret_column;
}

TextPosition TextDocumentModel::clamped_position(TextPosition position) const noexcept
{
    position.line = std::min(position.line, m_lines.size() - 1);
    position.column = clamp_to_character_boundary(m_lines[position.line], position.column);
    return position;
}

void TextDocumentModel::begin_or_clear_selection(
    bool extend_selection,
    TextPosition previous_caret) noexcept
{
    if (extend_selection)
    {
        static_cast<void>(previous_caret);
        return;
    }
    m_selection_anchor = {m_caret_line, m_caret_column};
}

void TextDocumentModel::set_diagnostics(std::vector<Language::Protocol::Diagnostic> diagnostics)
{
    m_diagnostics = std::move(diagnostics);
}

std::vector<Language::Protocol::Diagnostic> TextDocumentModel::get_diagnostics_for_line(std::size_t line) const
{
    std::vector<Language::Protocol::Diagnostic> line_diags;
    for (const auto& diag : m_diagnostics)
    {
        if (diag.range.start.line <= line && line <= diag.range.end.line)
        {
            line_diags.push_back(diag);
        }
    }
    return line_diags;
}

} // namespace Zenvra::UI::Editor
