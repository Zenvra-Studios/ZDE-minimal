/**
 * 
 * 
 */

#include "Terminal/TerminalPanelModel.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Terminal
{

/**
 * 
 * 
 */
bool TerminalPanelModel::toggle(const std::filesystem::path& working_directory)
{
    m_visible = !m_visible;
    m_focused = m_visible;
    if (m_visible && m_sessions.empty())
    {
        if (!create_session(working_directory))
        {
            m_visible = false;
            m_focused = false;
            return false;
        }
    }
    return true;
}

/**
 * 
 * 
 */
bool TerminalPanelModel::create_session(const std::filesystem::path& working_directory)
{
    if (m_sessions.size() >= maximum_sessions)
    {
        return false;
    }
    auto session = std::make_unique<TerminalSession>();
    if (!session->start(working_directory))
    {
        return false;
    }
    const std::size_t identifier = m_next_identifier++;
    const std::string shell_name = session->get_shell_path().stem().string();
    m_sessions.push_back(TerminalSessionEntry{
        .identifier = identifier,
        .title = shell_name,
        .session = std::move(session),
    });
    m_active_index = m_sessions.size() - 1;
    m_scroll_offset = 0;
    m_visible = true;
    m_focused = true;
    return true;
}

/**
 * 
 * 
 */
bool TerminalPanelModel::close_active_session()
{
    if (!m_active_index || *m_active_index >= m_sessions.size())
    {
        return false;
    }
    remove_session(*m_active_index);
    return true;
}

/**
 * 
 * 
 */
bool TerminalPanelModel::activate_session(std::size_t index) noexcept
{
    if (index >= m_sessions.size())
    {
        return false;
    }
    const bool changed = !m_active_index || *m_active_index != index;
    m_active_index = index;
    m_scroll_offset = 0;
    m_visible = true;
    m_focused = true;
    return changed;
}

/**
 * 
 * 
 */
void TerminalPanelModel::shutdown() noexcept
{
    for (TerminalSessionEntry& entry : m_sessions)
    {
        entry.session->stop();
    }
    m_sessions.clear();
    m_active_index.reset();
    m_visible = false;
    m_focused = false;
    m_scroll_offset = 0;
}

/**
 * 
 * 
 */
bool TerminalPanelModel::send_text(std::string_view text)
{
    TerminalSession* session = get_active_session();
    if (!m_visible || !m_focused || session == nullptr)
    {
        return false;
    }
    m_scroll_offset = 0;
    return session->write_input(text);
}

/**
 * 
 * 
 */
bool TerminalPanelModel::send_key(TerminalInputKey key)
{
    switch (key)
    {
    case TerminalInputKey::Enter: return send_text("\r");
    case TerminalInputKey::Backspace: return send_text("\x7F");
    case TerminalInputKey::Tab: return send_text("\t");
    case TerminalInputKey::Escape: return send_text("\x1B");
    case TerminalInputKey::ArrowUp: return send_text("\x1B[A");
    case TerminalInputKey::ArrowDown: return send_text("\x1B[B");
    case TerminalInputKey::ArrowRight: return send_text("\x1B[C");
    case TerminalInputKey::ArrowLeft: return send_text("\x1B[D");
    case TerminalInputKey::Home: return send_text("\x1B[H");
    case TerminalInputKey::End: return send_text("\x1B[F");
    case TerminalInputKey::DeleteForward: return send_text("\x1B[3~");
    }
    return false;
}

/**
 * 
 * 
 */
bool TerminalPanelModel::send_control(char letter)
{
    const unsigned char normalized = static_cast<unsigned char>(
        std::toupper(static_cast<unsigned char>(letter)));
    if (normalized < 'A' || normalized > 'Z')
    {
        return false;
    }
    const char control = static_cast<char>(normalized - 'A' + 1);
    return send_text(std::string_view{&control, 1});
}

/**
 * 
 * 
 */
bool TerminalPanelModel::poll()
{
    bool changed = false;
    std::size_t index = 0;
    while (index < m_sessions.size())
    {
        TerminalSession& session = *m_sessions[index].session;
        changed = session.poll() || changed;
        if (!session.is_running())
        {
            remove_session(index);
            changed = true;
            continue;
        }
        ++index;
    }
    return changed;
}

/**
 * 
 * 
 */
void TerminalPanelModel::remove_session(std::size_t index) noexcept
{
    if (index >= m_sessions.size())
    {
        return;
    }

    const bool removed_active = m_active_index && *m_active_index == index;
    m_sessions[index].session->stop();
    m_sessions.erase(m_sessions.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_sessions.empty())
    {
        m_active_index.reset();
        m_visible = false;
        m_focused = false;
        m_scroll_offset = 0;
        return;
    }

    if (!m_active_index)
    {
        m_active_index = 0;
    }
    else if (removed_active)
    {
        m_active_index = std::min(index, m_sessions.size() - 1);
        m_scroll_offset = 0;
    }
    else if (index < *m_active_index)
    {
        --*m_active_index;
    }
}

/**
 * 
 * 
 */
bool TerminalPanelModel::scroll(std::ptrdiff_t line_delta, std::size_t maximum_offset) noexcept
{
    if (get_active_session() == nullptr || line_delta == 0)
    {
        return false;
    }
    const std::size_t previous = m_scroll_offset;
    if (line_delta < 0)
    {
        m_scroll_offset = std::min(
            maximum_offset,
            m_scroll_offset + static_cast<std::size_t>(-line_delta));
    }
    else
    {
        const std::size_t amount = static_cast<std::size_t>(line_delta);
        m_scroll_offset = amount > m_scroll_offset ? 0 : m_scroll_offset - amount;
    }
    return previous != m_scroll_offset;
}

void TerminalPanelModel::resize(std::size_t columns, std::size_t rows) noexcept
{
    if (TerminalSession* session = get_active_session())
    {
        session->resize(columns, rows);
    }
}

/**
 * 
 * 
 */
bool TerminalPanelModel::is_visible() const noexcept 
{ 
    return m_visible; 
}

/**
 * 
 * 
 */
bool TerminalPanelModel::is_focused() const noexcept 
{ 
    return m_visible && m_focused; 
}

void TerminalPanelModel::set_focused(bool focused) noexcept
{
    m_focused = m_visible && focused;
}

/**
 * 
 * 
 */
std::size_t TerminalPanelModel::get_scroll_offset() const noexcept 
{ 
    return m_scroll_offset; 
}

std::optional<std::size_t> TerminalPanelModel::get_active_index() const noexcept
{
    return m_active_index;
}

std::span<const TerminalSessionEntry> TerminalPanelModel::get_sessions() const noexcept
{
    return m_sessions;
}

TerminalSession* TerminalPanelModel::get_active_session() noexcept
{
    return m_active_index && *m_active_index < m_sessions.size()
        ? m_sessions[*m_active_index].session.get()
        : nullptr;
}

const TerminalSession* TerminalPanelModel::get_active_session() const noexcept
{
    return m_active_index && *m_active_index < m_sessions.size()
        ? m_sessions[*m_active_index].session.get()
        : nullptr;
}

void TerminalPanelModel::start_selection(std::size_t line, std::size_t column) noexcept
{
    m_selection.start = TerminalPosition{line, column};
    m_selection.end = TerminalPosition{line, column};
}

void TerminalPanelModel::update_selection(std::size_t line, std::size_t column) noexcept
{
    m_selection.end = TerminalPosition{line, column};
}

void TerminalPanelModel::clear_selection() noexcept
{
    m_selection.start = TerminalPosition{0, 0};
    m_selection.end = TerminalPosition{0, 0};
}

void TerminalPanelModel::select_word(std::size_t line, std::size_t column) noexcept
{
    const TerminalSession* session = get_active_session();
    if (session == nullptr)
    {
        return;
    }
    const std::span<const std::string> lines = session->get_lines();
    if (line >= lines.size())
    {
        return;
    }
    const std::string& text = lines[line];
    if (text.empty())
    {
        return;
    }
    std::size_t col = std::min(column, text.size() - 1);
    
    auto is_word_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-' || c == '.';
    };

    if (!is_word_char(text[col]))
    {
        m_selection.start = TerminalPosition{line, col};
        m_selection.end = TerminalPosition{line, col + 1};
        return;
    }

    std::size_t start_col = col;
    while (start_col > 0 && is_word_char(text[start_col - 1]))
    {
        --start_col;
    }
    std::size_t end_col = col;
    while (end_col < text.size() && is_word_char(text[end_col]))
    {
        ++end_col;
    }
    m_selection.start = TerminalPosition{line, start_col};
    m_selection.end = TerminalPosition{line, end_col};
}

void TerminalPanelModel::select_line(std::size_t line) noexcept
{
    const TerminalSession* session = get_active_session();
    if (session == nullptr)
    {
        return;
    }
    const std::span<const std::string> lines = session->get_lines();
    if (line >= lines.size())
    {
        return;
    }
    m_selection.start = TerminalPosition{line, 0};
    m_selection.end = TerminalPosition{line, lines[line].size()};
}

bool TerminalPanelModel::has_selection() const noexcept
{
    return !m_selection.is_empty();
}

const TerminalSelection& TerminalPanelModel::get_selection() const noexcept
{
    return m_selection;
}

std::string TerminalPanelModel::get_selected_text() const
{
    const TerminalSession* session = get_active_session();
    if (session == nullptr || m_selection.is_empty())
    {
        return {};
    }
    return m_selection.extract_text(session->get_lines());
}

} // namespace Zenvra::Terminal
