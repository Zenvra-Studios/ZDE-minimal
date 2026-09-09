#include "Editors/Interaction/EditorInputRouter.h"

namespace Zenvra::Editors
{

EditorInputRouter::EditorInputRouter()
    : m_default_mode(std::make_unique<DefaultEditorMode>())
    , m_vim_mode(std::make_unique<VimEditorMode>())
    , m_active_mode(m_default_mode.get())
{
    m_active_mode->activate();
}

void EditorInputRouter::set_interaction_mode(InteractionMode mode)
{
    if (m_mode == mode)
    {
        return;
    }

    if (m_active_mode != nullptr)
    {
        m_active_mode->deactivate();
    }

    m_mode = mode;
    if (m_mode == InteractionMode::Vim)
    {
        m_active_mode = m_vim_mode.get();
    }
    else
    {
        m_active_mode = m_default_mode.get();
    }

    if (m_active_mode != nullptr)
    {
        m_active_mode->activate();
    }
}

void EditorInputRouter::set_interaction_mode(std::string_view mode_str)
{
    if (mode_str == "vim" || mode_str == "Vim" || mode_str == "VIM")
    {
        set_interaction_mode(InteractionMode::Vim);
    }
    else
    {
        set_interaction_mode(InteractionMode::Default);
    }
}

bool EditorInputRouter::handle_key(
    const EditorKeyEvent& event,
    TextDocumentModel& doc,
    EditorController& controller)
{
    if (m_active_mode != nullptr)
    {
        return m_active_mode->handle_key(event, doc, controller);
    }
    return false;
}

bool EditorInputRouter::handle_text_input(
    std::string_view utf8_text,
    TextDocumentModel& doc,
    EditorController& controller)
{
    if (m_active_mode != nullptr)
    {
        return m_active_mode->handle_text_input(utf8_text, doc, controller);
    }
    return false;
}

void EditorInputRouter::update(double delta_time_sec)
{
    if (m_active_mode != nullptr)
    {
        m_active_mode->update(delta_time_sec);
    }
}

std::string_view EditorInputRouter::get_mode_name() const noexcept
{
    if (m_active_mode != nullptr)
    {
        return m_active_mode->get_mode_name();
    }
    return "";
}

std::string_view EditorInputRouter::get_cursor_shape() const noexcept
{
    if (m_active_mode != nullptr)
    {
        return m_active_mode->get_cursor_shape();
    }
    return "Line";
}

bool EditorInputRouter::is_insert_mode() const noexcept
{
    if (m_active_mode != nullptr)
    {
        return m_active_mode->is_insert_mode();
    }
    return true;
}

} // namespace Zenvra::Editors
