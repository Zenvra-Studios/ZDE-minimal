#pragma once

#include "Editors/Interaction/EditorKeyEvent.h"
#include "UI/Editor/TextDocumentModel.h"

#include <string_view>

namespace Zenvra::UI::Editor
{
class EditorController;
}

namespace Zenvra::Editors
{

using UI::Editor::TextDocumentModel;
using UI::Editor::EditorController;

class IEditorInteractionMode
{
public:
    virtual ~IEditorInteractionMode() = default;

    virtual void activate() = 0;
    virtual void deactivate() = 0;

    virtual bool handle_key(
        const EditorKeyEvent& event,
        TextDocumentModel& doc,
        EditorController& controller) = 0;

    virtual bool handle_text_input(
        std::string_view utf8_text,
        TextDocumentModel& doc,
        EditorController& controller) = 0;

    virtual void update(double delta_time_sec) = 0;

    [[nodiscard]] virtual std::string_view get_mode_name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view get_cursor_shape() const noexcept = 0;
    [[nodiscard]] virtual bool is_insert_mode() const noexcept = 0;
};

} // namespace Zenvra::Editors

namespace Zenvra::UI::Editor
{
using IEditorInteractionMode = Zenvra::Editors::IEditorInteractionMode;
} // namespace Zenvra::UI::Editor
