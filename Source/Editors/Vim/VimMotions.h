#pragma once

#include "UI/Editor/TextDocumentModel.h"
#include "Editors/Vim/VimState.h"

#include <cstddef>
#include <string_view>

namespace Zenvra::Editors::VimMotions
{

using UI::Editor::TextDocumentModel;
using UI::Editor::TextPosition;

// Basic cursor movements
void move_left(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_right(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_up(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_down(TextDocumentModel& doc, int count, bool extend_selection = false);

// Line movements
void move_line_start(TextDocumentModel& doc, bool extend_selection = false);
void move_first_non_blank(TextDocumentModel& doc, bool extend_selection = false);
void move_line_end(TextDocumentModel& doc, bool extend_selection = false);

// Word movements
void move_word_forward(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_word_backward(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_word_end(TextDocumentModel& doc, int count, bool extend_selection = false);

// Big WORD movements (whitespace delimited)
void move_WORD_forward(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_WORD_backward(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_WORD_end(TextDocumentModel& doc, int count, bool extend_selection = false);

// Document movements
void move_document_start(TextDocumentModel& doc, int line_number = 0, bool extend_selection = false);
void move_document_end(TextDocumentModel& doc, int line_number = 0, bool extend_selection = false);

// Paragraph movements
void move_paragraph_backward(TextDocumentModel& doc, int count, bool extend_selection = false);
void move_paragraph_forward(TextDocumentModel& doc, int count, bool extend_selection = false);

// In-line find character motions (f, F, t, T, ;, ,)
bool find_char_on_line(TextDocumentModel& doc, char target, char find_type, int count, bool extend_selection = false);
bool repeat_find_char(TextDocumentModel& doc, const VimState& state, bool reverse, bool extend_selection = false);

// Matching bracket motion (%)
bool move_matching_bracket(TextDocumentModel& doc, bool extend_selection = false);

// Search motions
bool search_next(TextDocumentModel& doc, std::string_view query, bool forward, bool extend_selection = false);
bool search_word_under_cursor(TextDocumentModel& doc, VimState& state, bool forward);

// Calculation helpers for range determination
TextPosition get_word_forward_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_word_end_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_word_backward_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_WORD_forward_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_WORD_end_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_WORD_backward_target(const TextDocumentModel& doc, TextPosition start, int count);
TextPosition get_line_end_target(const TextDocumentModel& doc, TextPosition start);
TextPosition get_find_char_target(const TextDocumentModel& doc, TextPosition start, char target, char find_type, int count);
TextPosition get_matching_bracket_target(const TextDocumentModel& doc, TextPosition start);
TextPosition get_paragraph_target(const TextDocumentModel& doc, TextPosition start, bool forward, int count);

// Text Object range determination (iw, aw, i", a", i', a', i(, a(, i{, a{, i[, a[, etc.)
std::pair<TextPosition, TextPosition> get_text_object_range(
    const TextDocumentModel& doc, TextPosition pos, char modifier, char obj);

} // namespace Zenvra::Editors::VimMotions

namespace Zenvra::UI::Editor
{
namespace VimMotions = Zenvra::Editors::VimMotions;
} // namespace Zenvra::UI::Editor
