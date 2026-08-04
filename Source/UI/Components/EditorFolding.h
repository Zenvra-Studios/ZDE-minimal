#pragma once

#include <cstddef>
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
    None,       ///< No folding decoration on this line.
    Expanded,   ///< Start of a foldable range that is currently expanded (show [-]).
    Collapsed,  ///< Start of a foldable range that is currently collapsed (show [+]).
    Continuation, ///< Interior line of an expanded range (vertical guide line).
    End,        ///< Last line of an expanded range (corner guide ╰).
};

/// Computes and caches code-folding ranges and scope guide information for a
/// text document.  The model is intentionally platform-agnostic so that both
/// Win32 and X11 renderers can share the same folding logic.
class EditorFoldingModel
{
public:
    /// Re-analyse the document lines and rebuild the fold-range table.
    /// Call this whenever the document text changes.
    void rebuild(const std::vector<std::string>& lines);

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

private:
    std::vector<FoldRange> m_ranges;
    std::unordered_set<std::size_t> m_collapsed;
};

} // namespace Zenvra::UI::Components
