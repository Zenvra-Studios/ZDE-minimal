#pragma once

#include "UI/Editor/EditorSessionModel.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace Zenvra::UI::Editor
{

class EditorController
{
public:
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] std::size_t open_dropped_paths(
        std::span<const std::filesystem::path> dropped_paths);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool activate_file(std::size_t index) noexcept;
    [[nodiscard]] bool close_file(std::size_t index);
    [[nodiscard]] bool execute_action(EditorAction action);
    [[nodiscard]] bool can_execute_action(EditorAction action) const noexcept;
    [[nodiscard]] static std::optional<EditorAction> action_from_command_id(
        std::string_view command_id) noexcept;
    [[nodiscard]] bool execute_input(
        EditorInputCommand command,
        bool extend_selection = false);
    [[nodiscard]] bool insert_text(std::string_view utf8_text);

    [[nodiscard]] TextDocumentModel* get_active_document() noexcept;
    [[nodiscard]] const TextDocumentModel* get_active_document() const noexcept;
    [[nodiscard]] std::span<const EditorSessionDocument> get_documents() const noexcept;
    [[nodiscard]] std::optional<std::size_t> get_active_index() const noexcept;

private:
    EditorSessionModel m_session;
    std::string m_clipboard;
};

} // namespace Zenvra::UI::Editor
