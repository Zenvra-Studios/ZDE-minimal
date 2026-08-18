#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Editor
{

enum class SearchInputFocus
{
    None,
    Search,
    Replace
};

struct SearchHighlightSpan
{
    std::size_t start = 0;
    std::size_t length = 0;
};

struct SearchMatch
{
    std::size_t line_number = 1;         // 1-indexed
    std::size_t column_start = 0;        // 0-indexed in original line
    std::size_t match_length = 0;
    std::string line_content;            // Trimmed line preview text
    std::size_t match_preview_start = 0; // 0-indexed in line_content
    std::size_t match_preview_length = 0;
    std::vector<SearchHighlightSpan> spans; // All match spans in line_content
};

struct FileSearchResult
{
    std::filesystem::path file_path;
    std::string file_name;
    std::string relative_dir;
    std::vector<SearchMatch> matches;
    bool expanded = true;
};

enum class SearchRowKind
{
    FileHeader,
    MatchLine
};

struct SearchVisibleRow
{
    SearchRowKind kind = SearchRowKind::FileHeader;
    std::size_t file_index = 0;
    std::size_t match_index = 0;
};

struct SearchNavigationTarget
{
    std::filesystem::path path;
    std::size_t line = 1;
    std::size_t column = 0;
};

class WorkspaceSearchModel
{
public:
    void set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_workspace_root; }

    // Search Query & Replace Query
    void set_search_query(std::string_view query);
    [[nodiscard]] std::string_view get_search_query() const noexcept { return m_search_query; }

    void set_replace_query(std::string_view query);
    [[nodiscard]] std::string_view get_replace_query() const noexcept { return m_replace_query; }

    // Flags & Toggles
    void toggle_match_case() noexcept;
    void toggle_match_word() noexcept;
    void toggle_use_regex() noexcept;
    void toggle_preserve_case() noexcept;
    void toggle_replace_expanded() noexcept;

    [[nodiscard]] bool is_match_case() const noexcept { return m_match_case; }
    [[nodiscard]] bool is_match_word() const noexcept { return m_match_word; }
    [[nodiscard]] bool is_use_regex() const noexcept { return m_use_regex; }
    [[nodiscard]] bool is_preserve_case() const noexcept { return m_preserve_case; }
    [[nodiscard]] bool is_replace_expanded() const noexcept { return m_replace_expanded; }

    // Focus & Text Input Editing
    void set_focused_input(SearchInputFocus focus) noexcept;
    [[nodiscard]] SearchInputFocus get_focused_input() const noexcept { return m_focused_input; }

    void insert_char(char32_t codepoint);
    void insert_text(std::string_view text);
    void handle_backspace();
    void handle_delete();
    void handle_left(bool shift = false);
    void handle_right(bool shift = false);
    void handle_home(bool shift = false);
    void handle_end(bool shift = false);
    void clear_query();

    // Selection
    void select_all() noexcept;
    void clear_selection() noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] std::pair<std::size_t, std::size_t> get_selection_range() const noexcept;
    [[nodiscard]] std::string get_selected_text() const;
    void set_caret_and_selection(std::size_t caret, std::size_t sel_start, std::size_t sel_end) noexcept;
    void update_drag_selection(std::size_t caret) noexcept;

    [[nodiscard]] std::size_t get_search_caret() const noexcept { return m_search_caret; }
    [[nodiscard]] std::size_t get_replace_caret() const noexcept { return m_replace_caret; }

    // Execution
    void execute_search();
    bool tick() noexcept;
    void clear_results();
    void collapse_all();
    void expand_all();
    void toggle_file_expanded(std::size_t file_index);

    // Results & Rows
    [[nodiscard]] std::span<const FileSearchResult> get_results() const noexcept { return m_results; }
    [[nodiscard]] std::span<const SearchVisibleRow> get_visible_rows() const noexcept { return m_visible_rows; }
    [[nodiscard]] std::size_t get_total_match_count() const noexcept { return m_total_match_count; }
    [[nodiscard]] std::size_t get_total_file_count() const noexcept { return m_total_file_count; }
    [[nodiscard]] bool is_searching() const noexcept { return m_is_searching; }
    [[nodiscard]] const std::string& get_search_error() const noexcept { return m_search_error; }

    // Row Activation
    [[nodiscard]] std::optional<SearchNavigationTarget> activate_visible_row(std::size_t visible_row_index);

    // Replace Operations
    bool replace_in_file(std::size_t file_index);
    bool replace_all();

    // Scroll offset
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept { return m_scroll_offset; }
    void set_scroll_offset(std::size_t offset) noexcept { m_scroll_offset = offset; }
    bool scroll(std::ptrdiff_t delta, std::size_t viewport_rows) noexcept;

private:
    void rebuild_visible_rows();
    void trigger_async_search();

    std::filesystem::path m_workspace_root;
    std::string m_search_query;
    std::string m_replace_query;
    std::size_t m_search_caret = 0;
    std::size_t m_replace_caret = 0;
    std::size_t m_search_sel_start = 0;
    std::size_t m_search_sel_end = 0;
    std::size_t m_replace_sel_start = 0;
    std::size_t m_replace_sel_end = 0;

    bool m_match_case = false;
    bool m_match_word = false;
    bool m_use_regex = false;
    bool m_preserve_case = false;
    bool m_replace_expanded = false;

    SearchInputFocus m_focused_input = SearchInputFocus::Search;

    std::vector<FileSearchResult> m_results;
    std::vector<SearchVisibleRow> m_visible_rows;
    std::size_t m_total_match_count = 0;
    std::size_t m_total_file_count = 0;
    std::size_t m_scroll_offset = 0;
    bool m_is_searching = false;
    std::string m_search_error;

    // Asynchronous worker & file cache state
    std::atomic<std::uint64_t> m_search_generation{0};
    std::atomic<bool> m_cancel_requested{false};
    std::atomic<bool> m_results_ready{false};

    std::filesystem::path m_cached_search_root;
    std::vector<std::filesystem::path> m_cached_files;
    std::mutex m_cache_mutex;

    std::mutex m_staging_mutex;
    std::vector<FileSearchResult> m_staging_results;
    std::string m_staging_error;
    std::size_t m_staging_match_count = 0;
};

} // namespace Zenvra::UI::Editor
