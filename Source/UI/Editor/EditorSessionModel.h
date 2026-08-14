#pragma once

#include "UI/Editor/EditorFileCrud.h"
#include "UI/Editor/TextDocumentModel.h"

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace Zenvra::UI::Editor
{

enum class EditorAction
{
    CreateDocument,
    SaveDocument,
    CloseDocument,
    RemoveDocument,
    SelectAll,
    Copy,
    Cut,
    Paste,
    ToggleComment,
    MoveLineUp,
    MoveLineDown,
    AddCursorAbove,
    AddCursorBelow,
};

struct EditorSessionDocument
{
    std::filesystem::path path;
    TextDocumentModel text;
    bool temporary = false;
};

class EditorSessionModel
{
public:
    [[nodiscard]] bool open_file(const std::filesystem::path& path);
    [[nodiscard]] bool create_buffer();
    [[nodiscard]] bool create_file();
    [[nodiscard]] bool save_active_file();
    [[nodiscard]] bool close_active_file();
    [[nodiscard]] bool close_file(std::size_t index);
    [[nodiscard]] bool delete_active_file();
    [[nodiscard]] bool rename_active_file(const std::filesystem::path& destination);
    [[nodiscard]] bool activate_file(std::size_t index) noexcept;
    [[nodiscard]] bool reorder_file(std::size_t from_index, std::size_t to_index) noexcept;

    [[nodiscard]] TextDocumentModel* get_active_document() noexcept;
    [[nodiscard]] const TextDocumentModel* get_active_document() const noexcept;
    [[nodiscard]] const std::filesystem::path* get_active_path() const noexcept;
    [[nodiscard]] std::optional<std::size_t> get_active_index() const noexcept;
    [[nodiscard]] std::span<const EditorSessionDocument> get_documents() const noexcept;

private:
    [[nodiscard]] bool save_file(std::size_t index);
    void remove_from_session(std::size_t index);

    EditorFileCrud m_crud;
    std::vector<EditorSessionDocument> m_documents;
    std::optional<std::size_t> m_active_index;
    std::size_t m_untitled_counter = 0;
};

} // namespace Zenvra::UI::Editor
