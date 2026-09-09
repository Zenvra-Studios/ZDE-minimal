#pragma once

#include "UI/Editor/TextDocumentModel.h"
#include "Editors/Vim/VimState.h"

namespace Zenvra::Editors::VimVisualMode
{

using UI::Editor::TextDocumentModel;

void enter_visual(TextDocumentModel& doc, VimState& state);
void enter_visual_line(TextDocumentModel& doc, VimState& state);
void exit_visual(TextDocumentModel& doc, VimState& state);

bool handle_visual_key(TextDocumentModel& doc, VimState& state, char key);

} // namespace Zenvra::Editors::VimVisualMode

namespace Zenvra::UI::Editor
{
namespace VimVisualMode = Zenvra::Editors::VimVisualMode;
} // namespace Zenvra::UI::Editor
