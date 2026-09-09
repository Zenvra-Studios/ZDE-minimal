#pragma once

#include "Editors/Interaction/IEditorInteractionMode.h"
#include "Editors/Vim/VimKeySequence.h"
#include "Editors/Vim/VimState.h"

namespace Zenvra::Editors
{

class VimEditorMode final : public IEditorInteractionMode
{
public:
    VimEditorMode();
    ~VimEditorMode() override = default;

    void activate() override;
    void deactivate() override;

    bool handle_key(
        const EditorKeyEvent& event,
        TextDocumentModel& doc,
        EditorController& controller) override;

    bool handle_text_input(
        std::string_view utf8_text,
        TextDocumentModel& doc,
        EditorController& controller) override;

    void update(double delta_time_sec) override;

    [[nodiscard]] std::string_view get_mode_name() const noexcept override;
    [[nodiscard]] std::string_view get_cursor_shape() const noexcept override;
    [[nodiscard]] bool is_insert_mode() const noexcept override;

    [[nodiscard]] VimMode get_mode() const noexcept { return m_state.mode; }
    [[nodiscard]] const VimState& get_state() const noexcept { return m_state; }
    [[nodiscard]] VimState& get_state() noexcept { return m_state; }

    void set_mode(VimMode mode) noexcept;

private:
    bool handle_normal_key(char c, const EditorKeyEvent& event, TextDocumentModel& doc, EditorController& controller);
    bool handle_command_key(char c, TextDocumentModel& doc, EditorController& controller);
    bool execute_command_line(std::string_view command, TextDocumentModel& doc, EditorController& controller);

    VimState m_state;
    VimKeySequence m_sequence;
    mutable std::string m_mode_display;
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using VimEditorMode = Zenvra::Editors::VimEditorMode;
} // namespace Zenvra::UI::Editor
