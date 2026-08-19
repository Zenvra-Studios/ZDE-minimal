#pragma once

#include "SourceControl/GitRepository.h"
#include "SourceControl/GitTypes.h"
#include "UI/Editor/StudioEditorModel.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Editor
{

enum class SourceControlRowKind
{
    RepositoryHeader,
    SectionHeaderStaged,
    SectionHeaderUnstaged,
    StagedFile,
    UnstagedFile,
    UntrackedFile,
    GitGraphHeader,
    GitGraphCommit
};

struct SourceControlRow
{
    SourceControlRowKind kind;
    std::size_t file_index = 0; // index into staged_items, unstaged_items, or history
    std::filesystem::path path;
    std::string label;
    std::string relative_dir;
    std::string commit_sha;
    std::string commit_short_sha;
    std::string commit_author;
    std::string commit_branch;
    bool is_head = false;
    Git::GitFileStatus status = Git::GitFileStatus::Unmodified;
    bool is_staged = false;
};

class WorkspaceSourceControlModel
{
public:
    WorkspaceSourceControlModel();
    ~WorkspaceSourceControlModel();

    void set_workspace_root(const std::filesystem::path& root);
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_workspace_root; }
    [[nodiscard]] bool is_git_repository() const noexcept;

    void refresh_status();
    [[nodiscard]] bool tick(); // returns true if view needs repaint

    // Repository status query
    [[nodiscard]] const Git::GitRepositoryStatus& get_status() const noexcept { return m_status; }
    [[nodiscard]] const std::vector<Git::GitCommitInfo>& get_history() const noexcept { return m_history; }
    [[nodiscard]] std::string get_active_branch() const;
    [[nodiscard]] std::size_t get_total_changes_count() const noexcept;

    // Visible rows for sidebar tree rendering
    [[nodiscard]] const std::vector<SourceControlRow>& get_visible_rows() const noexcept { return m_visible_rows; }
    [[nodiscard]] std::size_t get_scroll_offset() const noexcept { return m_scroll_offset; }
    void set_scroll_offset(std::size_t offset) noexcept { m_scroll_offset = offset; }
    bool scroll(std::ptrdiff_t delta, std::size_t viewport_rows) noexcept;

    // Sections collapse toggle
    void toggle_repo_collapsed() noexcept { m_repo_collapsed = !m_repo_collapsed; rebuild_visible_rows(); }
    void toggle_staged_collapsed() noexcept { m_staged_collapsed = !m_staged_collapsed; rebuild_visible_rows(); }
    void toggle_unstaged_collapsed() noexcept { m_unstaged_collapsed = !m_unstaged_collapsed; rebuild_visible_rows(); }
    void toggle_git_graph_collapsed() noexcept { m_git_graph_collapsed = !m_git_graph_collapsed; rebuild_visible_rows(); }

    [[nodiscard]] bool is_repo_collapsed() const noexcept { return m_repo_collapsed; }
    [[nodiscard]] bool is_staged_collapsed() const noexcept { return m_staged_collapsed; }
    [[nodiscard]] bool is_unstaged_collapsed() const noexcept { return m_unstaged_collapsed; }
    [[nodiscard]] bool is_git_graph_collapsed() const noexcept { return m_git_graph_collapsed; }

    // Commit Message Box Input & Caret
    [[nodiscard]] const std::string& get_commit_message() const noexcept { return m_commit_message; }
    void set_commit_message(std::string_view msg);
    [[nodiscard]] std::size_t get_caret() const noexcept { return m_caret; }
    [[nodiscard]] bool is_input_focused() const noexcept { return m_input_focused; }
    void set_input_focused(bool focused) noexcept { m_input_focused = focused; }
    void insert_text(std::string_view text);
    void handle_backspace();
    void handle_delete();
    void handle_left(bool extend_selection);
    void handle_right(bool extend_selection);
    void handle_home(bool extend_selection);
    void handle_end(bool extend_selection);
    void select_all();
    [[nodiscard]] bool has_selection() const noexcept { return m_selection_anchor.has_value() && *m_selection_anchor != m_caret; }
    [[nodiscard]] std::pair<std::size_t, std::size_t> get_selection_range() const noexcept;
    [[nodiscard]] std::string get_selected_text() const;
    void set_caret_and_selection(std::size_t anchor, std::size_t caret);

    // Git Actions
    [[nodiscard]] bool commit();
    [[nodiscard]] bool stage_file(const std::filesystem::path& path);
    [[nodiscard]] bool unstage_file(const std::filesystem::path& path);
    [[nodiscard]] bool stage_all();
    [[nodiscard]] bool unstage_all();
    [[nodiscard]] bool discard_changes(const std::filesystem::path& path);

    // Diff / Open File
    [[nodiscard]] std::optional<std::filesystem::path> activate_visible_row(std::size_t visible_index);

    // Git instance access
    [[nodiscard]] Git::GitRepository& get_repository() noexcept { return m_repo; }
    [[nodiscard]] const Git::GitRepository& get_repository() const noexcept { return m_repo; }

    [[nodiscard]] const std::string& get_last_error() const noexcept { return m_last_error; }

private:
    void rebuild_visible_rows();

    Git::GitRepository m_repo;
    std::filesystem::path m_workspace_root;
    Git::GitRepositoryStatus m_status;
    std::vector<Git::GitCommitInfo> m_history;
    std::vector<SourceControlRow> m_visible_rows;
    std::size_t m_scroll_offset = 0;

    bool m_repo_collapsed = false;
    bool m_staged_collapsed = false;
    bool m_unstaged_collapsed = false;
    bool m_git_graph_collapsed = false;

    std::string m_commit_message;
    std::size_t m_caret = 0;
    std::optional<std::size_t> m_selection_anchor;
    bool m_input_focused = false;
    std::string m_last_error;

    std::atomic<bool> m_needs_refresh{false};
    std::chrono::steady_clock::time_point m_last_refresh_time;
};

} // namespace Zenvra::UI::Editor
