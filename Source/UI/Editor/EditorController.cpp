#include "UI/Editor/EditorController.h"

#include "Commands/CommandIds.h"
#include "UI/Editor/EditorDropModel.h"

namespace Zenvra::UI::Editor {

bool EditorController::open_file(const std::filesystem::path &path) {
  return m_session.open_file(path);
}

std::vector<std::size_t> EditorController::reload_externally_modified_files() {
  return m_session.reload_externally_modified_files();
}

std::size_t EditorController::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths) {
  std::size_t opened_count = 0;
  for (const std::filesystem::path &file :
       EditorDropModel::collect_files(dropped_paths)) {
    if (m_session.open_file(file)) {
      ++opened_count;
    }
  }
  return opened_count;
}

bool EditorController::create_buffer() { return m_session.create_buffer(); }

bool EditorController::activate_file(std::size_t index) noexcept {
  return m_session.activate_file(index);
}

bool EditorController::reorder_file(std::size_t from_index,
                                    std::size_t to_index) noexcept {
  return m_session.reorder_file(from_index, to_index);
}

bool EditorController::close_file(std::size_t index) {
  return m_session.close_file(index);
}

bool EditorController::close_all_files() {
  return m_session.close_all_files();
}

bool EditorController::execute_action(EditorAction action) {
  TextDocumentModel *document = m_session.get_active_document();
  switch (action) {
  case EditorAction::CreateDocument:
    return m_session.create_buffer();
  case EditorAction::SaveDocument:
    return m_session.save_active_file();
  case EditorAction::CloseDocument:
    return m_session.close_active_file();
  case EditorAction::RemoveDocument:
    return m_session.delete_active_file();
  case EditorAction::SelectAll:
    return document != nullptr && document->select_all();
  case EditorAction::Copy:
    if (document == nullptr || !document->has_selection()) {
      return false;
    }
    m_clipboard = document->get_selected_text();
    return true;
  case EditorAction::Cut:
    if (document == nullptr || document->is_read_only() ||
        !document->has_selection()) {
      return false;
    }
    m_clipboard = document->get_selected_text();
    return document->delete_selection();
  case EditorAction::Paste:
    return document != nullptr && !m_clipboard.empty() &&
           document->insert_text(m_clipboard);
  case EditorAction::ToggleComment:
    return document != nullptr && document->toggle_line_comment();
  case EditorAction::MoveLineUp:
    return document != nullptr && !document->is_read_only() &&
           document->move_line_up();
  case EditorAction::MoveLineDown:
    return document != nullptr && !document->is_read_only() &&
           document->move_line_down();
  case EditorAction::AddCursorAbove:
    return document != nullptr && document->add_cursor_above();
  case EditorAction::AddCursorBelow:
    return document != nullptr && document->add_cursor_below();
  }
  return false;
}

bool EditorController::can_execute_action(EditorAction action) const noexcept {
  const TextDocumentModel *document = m_session.get_active_document();
  switch (action) {
  case EditorAction::CreateDocument:
    return true;
  case EditorAction::SaveDocument:
    return document != nullptr && !document->is_read_only();
  case EditorAction::CloseDocument:
  case EditorAction::RemoveDocument:
  case EditorAction::SelectAll:
    return document != nullptr;
  case EditorAction::Copy:
    return document != nullptr && document->has_selection();
  case EditorAction::Cut:
    return document != nullptr && !document->is_read_only() &&
           document->has_selection();
  case EditorAction::Paste:
    return document != nullptr && !document->is_read_only() &&
           !m_clipboard.empty();
  case EditorAction::ToggleComment:
  case EditorAction::MoveLineUp:
  case EditorAction::MoveLineDown:
  case EditorAction::AddCursorAbove:
  case EditorAction::AddCursorBelow:
    return document != nullptr && !document->is_read_only();
  }
  return false;
}

std::optional<EditorAction>
EditorController::action_from_command_id(std::string_view command_id) noexcept {
  using namespace Commands::CommandIds;
  if (command_id == file_new) {
    return EditorAction::CreateDocument;
  }
  if (command_id == file_save) {
    return EditorAction::SaveDocument;
  }
  if (command_id == file_close) {
    return EditorAction::CloseDocument;
  }
  if (command_id == file_delete) {
    return EditorAction::RemoveDocument;
  }
  if (command_id == selection_select_all) {
    return EditorAction::SelectAll;
  }
  if (command_id == edit_copy) {
    return EditorAction::Copy;
  }
  if (command_id == edit_cut) {
    return EditorAction::Cut;
  }
  if (command_id == edit_paste) {
    return EditorAction::Paste;
  }
  if (command_id == edit_toggle_comment) {
    return EditorAction::ToggleComment;
  }
  if (command_id == selection_move_line_up) {
    return EditorAction::MoveLineUp;
  }
  if (command_id == selection_move_line_down) {
    return EditorAction::MoveLineDown;
  }
  if (command_id == selection_add_cursor_above) {
    return EditorAction::AddCursorAbove;
  }
  if (command_id == selection_add_cursor_below) {
    return EditorAction::AddCursorBelow;
  }
  return std::nullopt;
}

bool EditorController::execute_input(EditorInputCommand command,
                                     bool extend_selection) {
  TextDocumentModel *document = m_session.get_active_document();
  return document != nullptr && document->execute(command, extend_selection);
}

bool EditorController::insert_text(std::string_view utf8_text) {
  TextDocumentModel *document = m_session.get_active_document();
  return document != nullptr && document->insert_text(utf8_text);
}

TextDocumentModel *EditorController::get_active_document() noexcept {
  return m_session.get_active_document();
}

const TextDocumentModel *
EditorController::get_active_document() const noexcept {
  return m_session.get_active_document();
}

const std::filesystem::path *
EditorController::get_active_path() const noexcept {
  return m_session.get_active_path();
}

std::span<const EditorSessionDocument>
EditorController::get_documents() const noexcept {
  return m_session.get_documents();
}

std::optional<std::size_t> EditorController::get_active_index() const noexcept {
  return m_session.get_active_index();
}

} // namespace Zenvra::UI::Editor
