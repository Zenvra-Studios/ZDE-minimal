#pragma once

#include "Editors/Interaction/IEditorInteractionMode.h"

namespace Zenvra::Editors
{

class DefaultEditorMode final : public IEditorInteractionMode
{
public:
    DefaultEditorMode() = default;
    ~DefaultEditorMode() override = default;

    void activate() override {}
    void deactivate() override {}

    bool handle_key(
        const EditorKeyEvent& event,
        TextDocumentModel& doc,
        EditorController& controller) override;

    bool handle_text_input(
        std::string_view utf8_text,
        TextDocumentModel& doc,
        EditorController& controller) override;

    void update(double delta_time_sec) override;

    [[nodiscard]] std::string_view get_mode_name() const noexcept override { return ""; }
    [[nodiscard]] std::string_view get_cursor_shape() const noexcept override { return "Line"; }
    [[nodiscard]] bool is_insert_mode() const noexcept override { return true; }
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using DefaultEditorMode = Zenvra::Editors::DefaultEditorMode;
} // namespace Zenvra::UI::Editor
