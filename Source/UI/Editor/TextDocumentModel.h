#pragma once

#include "Language/Protocol/LspTypes.h"
#include "UI/Editor/StudioEditorModel.h"

#include <cstddef>
#include <compare>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <memory>

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
    MoveLineUp,
    MoveLineDown,
    AddCursorAbove,
    AddCursorBelow,
    Escape,
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

struct TextCursor
{
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t preferred_column = 0;
    TextPosition selection_anchor{0, 0};

    [[nodiscard]] bool has_selection() const noexcept
    {
        return selection_anchor != TextPosition{line, column};
    }

    [[nodiscard]] TextSelection get_selection() const noexcept
    {
        const TextPosition caret{line, column};
        return selection_anchor <= caret
            ? TextSelection{selection_anchor, caret}
            : TextSelection{caret, selection_anchor};
    }

    auto operator<=>(const TextCursor& other) const noexcept
    {
        if (auto cmp = line <=> other.line; cmp != 0) return cmp;
        return column <=> other.column;
    }
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
    void reload_contents(
        std::vector<std::string> lines,
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
    [[nodiscard]] std::size_t get_revision() const noexcept { return m_revision; }
    [[nodiscard]] std::vector<TextCursor> get_all_cursors() const;
    [[nodiscard]] bool has_secondary_cursors() const noexcept;

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
    bool move_line_up();
    bool move_line_down();
    bool add_cursor_above();
    bool add_cursor_below();
    bool clear_secondary_cursors() noexcept;
    void mark_saved() noexcept;

    void set_diagnostics(std::vector<Language::Protocol::Diagnostic> diagnostics);
    [[nodiscard]] std::vector<Language::Protocol::Diagnostic> get_diagnostics() const;
    [[nodiscard]] std::vector<Language::Protocol::Diagnostic> get_diagnostics_for_line(std::size_t line) const;
    [[nodiscard]] Language::Syntax::TokenizerState get_line_state(std::size_t line_index) const noexcept;

    bool toggle_breakpoint(std::size_t line_index);
    [[nodiscard]] bool has_breakpoint(std::size_t line_index) const noexcept;
    [[nodiscard]] const std::unordered_set<std::size_t>& get_breakpoints() const noexcept;
    void clear_all_breakpoints() noexcept;

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
    std::vector<TextCursor> m_secondary_cursors;
    mutable std::shared_ptr<std::mutex> m_diagnostics_mutex = std::make_shared<std::mutex>();
    std::vector<Language::Protocol::Diagnostic> m_diagnostics;
    std::unordered_set<std::size_t> m_breakpoints;
    mutable std::vector<Language::Syntax::TokenizerState> m_line_states;
    mutable std::size_t m_line_states_valid_up_to = 0;
    mutable std::size_t m_line_states_revision = 0;
    std::size_t m_revision = 0;
    bool m_dirty = false;
    bool m_read_only = false;
};

} // namespace Zenvra::UI::Editor
