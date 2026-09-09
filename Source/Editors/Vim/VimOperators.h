#pragma once

#include "UI/Editor/TextDocumentModel.h"
#include "Editors/Vim/VimState.h"

#include <string_view>

namespace Zenvra::Editors::VimOperators
{

using UI::Editor::TextDocumentModel;

// Character deletions
bool delete_character_under_cursor(TextDocumentModel& doc, VimState& state, int count);
bool delete_character_before_cursor(TextDocumentModel& doc, VimState& state, int count);

// Motion-based operations
bool execute_delete_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count);
bool execute_change_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count);
bool execute_yank_motion(TextDocumentModel& doc, VimState& state, std::string_view motion, int count);

// Whole line operations
bool delete_lines(TextDocumentModel& doc, VimState& state, int count);
bool change_lines(TextDocumentModel& doc, VimState& state, int count);
bool yank_lines(TextDocumentModel& doc, VimState& state, int count);

// Text object operations (diw, caw, ya", ci(, etc.)
bool execute_text_object_operation(
    TextDocumentModel& doc, VimState& state, char op, char modifier, char obj);

// In-line find operations when an operator is pending (e.g. dfx, dt", etc.)
bool execute_find_motion(
    TextDocumentModel& doc, VimState& state, char op, char target, char find_type, int count);

// Single character replace ('r')
bool replace_character(TextDocumentModel& doc, VimState& state, char new_char);

// Character / Line substitution ('s', 'S')
bool substitute_character(TextDocumentModel& doc, VimState& state, int count);
bool substitute_line(TextDocumentModel& doc, VimState& state, int count);

// Line joining ('J')
bool join_lines(TextDocumentModel& doc, VimState& state, int count);

// Case toggling ('~')
bool toggle_case_character(TextDocumentModel& doc, VimState& state, int count);

// Indent / Unindent ('>>', '<<')
bool indent_lines(TextDocumentModel& doc, VimState& state, int count, int tab_size = 4);
bool unindent_lines(TextDocumentModel& doc, VimState& state, int count, int tab_size = 4);

// Put / Paste
bool put_after(TextDocumentModel& doc, VimState& state, int count);
bool put_before(TextDocumentModel& doc, VimState& state, int count);

// Undo / Redo
bool undo(TextDocumentModel& doc);
bool redo(TextDocumentModel& doc);

} // namespace Zenvra::Editors::VimOperators

namespace Zenvra::UI::Editor
{
namespace VimOperators = Zenvra::Editors::VimOperators;
} // namespace Zenvra::UI::Editor
