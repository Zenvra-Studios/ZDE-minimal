#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <cstddef>
#include <compare>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Editor
{

enum class EditorInputCommand
{
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    MoveHome,
    MoveEnd,
    InsertNewLine,
    InsertTab,
    DeleteBackward,
    DeleteForward,
};

struct TextPosition
{
    std::size_t line = 0;
    std::size_t column = 0;

    auto operator<=>(const TextPosition&) const = default;
};

struct TextSelection
{
    TextPosition start;
    TextPosition end;
};

class TextDocumentModel
{
public:
    TextDocumentModel();

    void replace_contents(
        std::vector<std::string> lines,
        std::string file_name,
        std::vector<BreadcrumbItem> breadcrumbs,
        std::string line_ending,
        bool read_only = false);
    void update_file_identity(
        std::string file_name,
        std::vector<BreadcrumbItem> breadcrumbs,
        std::string line_ending);

    [[nodiscard]] std::size_t get_line_count() const noexcept;
    [[nodiscard]] std::string_view get_line(std::size_t line_index) const noexcept;
    [[nodiscard]] std::size_t get_caret_line() const noexcept;
    [[nodiscard]] std::size_t get_caret_column() const noexcept;
    [[nodiscard]] std::string_view get_file_name() const noexcept;
    [[nodiscard]] std::span<const BreadcrumbItem> get_breadcrumbs() const noexcept;
    [[nodiscard]] std::vector<BreadcrumbItem> get_full_breadcrumbs() const;
    [[nodiscard]] FooterEditorStatus get_status() const noexcept;
    [[nodiscard]] bool is_dirty() const noexcept;
    [[nodiscard]] bool is_read_only() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] TextSelection get_selection() const noexcept;
    [[nodiscard]] std::string get_selected_text() const;
    [[nodiscard]] std::span<const std::string> get_lines() const noexcept;

    bool set_caret(
        std::size_t line_index,
        std::size_t byte_column,
        bool extend_selection = false) noexcept;
    bool select_all() noexcept;
    bool select_word_at(std::size_t line_index, std::size_t byte_column);
    bool select_line_at(std::size_t line_index) noexcept;
    bool clear_selection() noexcept;
    bool insert_text(std::string_view utf8_text);
    bool execute(EditorInputCommand command, bool extend_selection = false);
    bool delete_selection();
    bool toggle_line_comment();
    void mark_saved() noexcept;

private:
    void insert_new_line();
    void delete_backward();
    void delete_forward();
    void update_preferred_column() noexcept;
    [[nodiscard]] TextPosition clamped_position(TextPosition position) const noexcept;
    void begin_or_clear_selection(bool extend_selection, TextPosition previous_caret) noexcept;

    std::vector<std::string> m_lines;
    std::vector<BreadcrumbItem> m_breadcrumbs;
    std::string m_file_name;
    std::string m_line_ending = "LF";
    std::size_t m_caret_line = 0;
    std::size_t m_caret_column = 0;
    std::size_t m_preferred_column = 0;
    TextPosition m_selection_anchor;
    bool m_dirty = false;
    bool m_read_only = false;
};

} // namespace Zenvra::UI::Editor
