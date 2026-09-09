#pragma once

#include "Editors/Interaction/IEditorInteractionMode.h"
#include "Editors/Interaction/DefaultEditorMode.h"
#include "Editors/Vim/VimEditorMode.h"

#include <memory>
#include <string_view>

namespace Zenvra::Editors
{

enum class InteractionMode
{
    Default,
    Vim
};

class EditorInputRouter
{
public:
    EditorInputRouter();
    ~EditorInputRouter() = default;

    void set_interaction_mode(InteractionMode mode);
    void set_interaction_mode(std::string_view mode_str);

    [[nodiscard]] InteractionMode get_interaction_mode() const noexcept { return m_mode; }
    [[nodiscard]] bool is_vim_active() const noexcept { return m_mode == InteractionMode::Vim; }

    bool handle_key(
        const EditorKeyEvent& event,
        TextDocumentModel& doc,
        EditorController& controller);

    bool handle_text_input(
        std::string_view utf8_text,
        TextDocumentModel& doc,
        EditorController& controller);

    void update(double delta_time_sec);

    [[nodiscard]] std::string_view get_mode_name() const noexcept;
    [[nodiscard]] std::string_view get_cursor_shape() const noexcept;
    [[nodiscard]] bool is_insert_mode() const noexcept;

    [[nodiscard]] VimEditorMode* get_vim_mode() noexcept { return m_vim_mode.get(); }
    [[nodiscard]] const VimEditorMode* get_vim_mode() const noexcept { return m_vim_mode.get(); }

private:
    InteractionMode m_mode = InteractionMode::Default;
    std::unique_ptr<DefaultEditorMode> m_default_mode;
    std::unique_ptr<VimEditorMode> m_vim_mode;
    IEditorInteractionMode* m_active_mode = nullptr;
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using InteractionMode = Zenvra::Editors::InteractionMode;
using EditorInputRouter = Zenvra::Editors::EditorInputRouter;
} // namespace Zenvra::UI::Editor
