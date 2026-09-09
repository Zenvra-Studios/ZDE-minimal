#include "Editors/Interaction/DefaultEditorMode.h"
#include "UI/Editor/EditorController.h"

namespace Zenvra::Editors
{

bool DefaultEditorMode::handle_key(
    const EditorKeyEvent& /*event*/,
    TextDocumentModel& /*doc*/,
    EditorController& /*controller*/)
{
    // In default mode, key routing falls back to standard editor input pipeline
    return false;
}

bool DefaultEditorMode::handle_text_input(
    std::string_view utf8_text,
    TextDocumentModel& doc,
    EditorController& /*controller*/)
{
    return doc.insert_text(utf8_text);
}

void DefaultEditorMode::update(double /*delta_time_sec*/)
{
}

} // namespace Zenvra::Editors
