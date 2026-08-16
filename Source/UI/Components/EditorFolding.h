#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Zenvra::UI::Components
{

/// Represents a foldable block of code (e.g. brace-delimited scope).
struct FoldRange
{
    std::size_t start_line = 0; ///< Line containing the opening brace.
    std::size_t end_line = 0;   ///< Line containing the closing brace.
    std::size_t indent_level = 0; ///< Visual indentation depth (in columns).
};

/// Determines the visual state of a single line in the folding margin.
enum class FoldMarker
{
    NoneMarker, ///< No folding decoration on this line.
    Expanded,   ///< Start of a foldable range that is currently expanded (show [-]).
    Collapsed,  ///< Start of a foldable range that is currently collapsed (show [+]).
    Continuation, ///< Interior line of an expanded range (vertical guide line).
    End,        ///< Last line of an expanded range (corner guide ╰).
};

/// Scope information for the active indent guide highlight near the caret.
struct ActiveIndentScope
{
    std::size_t start_line = 0;
    std::size_t end_line = 0;
    std::size_t column = 0;
    bool valid = false;
};

/// Computes and caches code-folding ranges and scope guide information for a
/// text document.  The model is intentionally platform-agnostic so that both
/// Win32 and X11 renderers can share the same folding logic.
class EditorFoldingModel
{
public:
    /// Re-analyse the document lines and rebuild the fold-range table and indent guides.
    /// Call this whenever the document text changes.
    void rebuild(std::span<const std::string> lines, std::size_t tab_size = 4);

    /// Toggle the collapsed state of the fold that starts at `line_index`.
    /// Returns true if the state actually changed.
    bool toggle_fold(std::size_t line_index);

    /// Returns true if `line_index` should be hidden because a parent fold is
    /// collapsed.
    [[nodiscard]] bool is_line_hidden(std::size_t line_index) const noexcept;

    /// Returns the fold marker for the given line (used by the gutter renderer).
    [[nodiscard]] FoldMarker get_marker(std::size_t line_index) const noexcept;

    /// Returns all computed fold ranges (useful for drawing scope guide lines).
    [[nodiscard]] const std::vector<FoldRange>& get_ranges() const noexcept;

    /// Returns the set of collapsed start-lines.
    [[nodiscard]] const std::unordered_set<std::size_t>& get_collapsed() const noexcept;

    /// Returns the fold range that starts at `line_index`, or nullptr if none.
    [[nodiscard]] const FoldRange* get_range_at(std::size_t line_index) const noexcept;

    /// Check if a line is the start of any fold range.
    [[nodiscard]] bool is_fold_start(std::size_t line_index) const noexcept;

    /// Returns effective indentation depth (in columns) for a given line.
    /// Blank lines inherit indentation from surrounding context.
    [[nodiscard]] std::size_t get_effective_indent(std::size_t line_index) const noexcept;

    /// Returns the active indent guide scope enclosing `caret_line`.
    [[nodiscard]] ActiveIndentScope get_active_indent_scope(
        std::size_t caret_line,
        std::size_t tab_size = 4) const noexcept;

    /// Returns pointers to all expanded (non-collapsed) fold ranges whose
    /// line span overlaps the viewport [first_line, last_line].
    [[nodiscard]] std::vector<const FoldRange*> get_indent_guide_ranges(
        std::size_t first_line,
        std::size_t last_line) const;

    /// Returns the innermost expanded fold range that contains `line_index`,
    /// or nullptr if the line is not inside any expanded range.
    [[nodiscard]] const FoldRange* get_active_indent_range(
        std::size_t line_index) const noexcept;

private:
    std::vector<FoldRange> m_ranges;
    std::unordered_set<std::size_t> m_collapsed;
    std::vector<std::size_t> m_effective_indents;
};

} // namespace Zenvra::UI::Components
