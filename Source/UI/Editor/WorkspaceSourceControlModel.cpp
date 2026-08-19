#include "UI/Editor/WorkspaceSourceControlModel.h"

#include <algorithm>

namespace Zenvra::UI::Editor
{

WorkspaceSourceControlModel::WorkspaceSourceControlModel()
{
    m_last_refresh_time = std::chrono::steady_clock::now();
}

WorkspaceSourceControlModel::~WorkspaceSourceControlModel() = default;

void WorkspaceSourceControlModel::set_workspace_root(const std::filesystem::path& root)
{
    m_workspace_root = root;
    if (!root.empty())
    {
        if (m_repo.open(root))
        {
            refresh_status();
        }
        else
        {
            m_status = {};
            m_history.clear();
            m_visible_rows.clear();
        }
    }
    else
    {
        m_repo.close();
        m_status = {};
        m_history.clear();
        m_visible_rows.clear();
    }
}

bool WorkspaceSourceControlModel::is_git_repository() const noexcept
{
    return m_repo.is_open();
}

std::string WorkspaceSourceControlModel::get_active_branch() const
{
    if (!m_status.active_branch.empty())
    {
        return m_status.active_branch;
    }
    return "main";
}

std::size_t WorkspaceSourceControlModel::get_total_changes_count() const noexcept
{
    return m_status.staged_items.size() + m_status.unstaged_items.size() + m_status.untracked_items.size();
}

void WorkspaceSourceControlModel::refresh_status()
{
    if (!m_repo.is_open())
    {
        m_status = {};
        m_history.clear();
        m_visible_rows.clear();
        return;
    }

    m_status = m_repo.get_status();
    m_history = m_repo.get_history(50);
    rebuild_visible_rows();
    m_last_refresh_time = std::chrono::steady_clock::now();
}

bool WorkspaceSourceControlModel::tick()
{
    const auto now = std::chrono::steady_clock::now();
    // Auto-poll repo status every 2 seconds if open
    if (m_repo.is_open() && std::chrono::duration_cast<std::chrono::seconds>(now - m_last_refresh_time).count() >= 2)
    {
        refresh_status();
        return true;
    }
    return false;
}

void WorkspaceSourceControlModel::rebuild_visible_rows()
{
    m_visible_rows.clear();
    if (!m_repo.is_open()) return;

    // 1. Repository Header Row (e.g. ⌄ ZDE-minimal   main*)
    SourceControlRow repo_header{};
    repo_header.kind = SourceControlRowKind::RepositoryHeader;
    repo_header.label = m_workspace_root.filename().string();
    if (repo_header.label.empty()) repo_header.label = "workspace";
    repo_header.commit_branch = m_status.active_branch.empty() ? "main" : m_status.active_branch;
    if (get_total_changes_count() > 0)
    {
        repo_header.commit_branch += "*";
    }
    m_visible_rows.push_back(std::move(repo_header));

    if (!m_repo_collapsed)
    {
        // 2. Staged Section
        if (!m_status.staged_items.empty())
        {
            SourceControlRow header{};
            header.kind = SourceControlRowKind::SectionHeaderStaged;
            header.label = "Staged Changes";
            m_visible_rows.push_back(std::move(header));

            if (!m_staged_collapsed)
            {
                for (std::size_t i = 0; i < m_status.staged_items.size(); ++i)
                {
                    const auto& item = m_status.staged_items[i];
                    SourceControlRow row{};
                    row.kind = SourceControlRowKind::StagedFile;
                    row.file_index = i;
                    row.path = item.path;
                    row.label = item.path.filename().string();
                    row.relative_dir = item.path.parent_path().generic_string();
                    row.status = item.status;
                    row.is_staged = true;
                    m_visible_rows.push_back(std::move(row));
                }
            }
        }

        // 3. Unstaged / Working Tree Changes Section
        SourceControlRow changes_header{};
        changes_header.kind = SourceControlRowKind::SectionHeaderUnstaged;
        changes_header.label = "Changes";
        m_visible_rows.push_back(std::move(changes_header));

        if (!m_unstaged_collapsed)
        {
            for (std::size_t i = 0; i < m_status.unstaged_items.size(); ++i)
            {
                const auto& item = m_status.unstaged_items[i];
                SourceControlRow row{};
                row.kind = SourceControlRowKind::UnstagedFile;
                row.file_index = i;
                row.path = item.path;
                row.label = item.path.filename().string();
                row.relative_dir = item.path.parent_path().generic_string();
                row.status = item.status;
                row.is_staged = false;
                m_visible_rows.push_back(std::move(row));
            }

            for (std::size_t i = 0; i < m_status.untracked_items.size(); ++i)
            {
                const auto& item = m_status.untracked_items[i];
                SourceControlRow row{};
                row.kind = SourceControlRowKind::UntrackedFile;
                row.file_index = i;
                row.path = item.path;
                row.label = item.path.filename().string();
                row.relative_dir = item.path.parent_path().generic_string();
                row.status = Git::GitFileStatus::Untracked;
                row.is_staged = false;
                m_visible_rows.push_back(std::move(row));
            }
        }
    }

    // 4. Git Graph / Commits Section
    if (!m_history.empty())
    {
        SourceControlRow graph_header{};
        graph_header.kind = SourceControlRowKind::GitGraphHeader;
        graph_header.label = "Git Graph";
        graph_header.relative_dir = m_workspace_root.filename().string();
        m_visible_rows.push_back(std::move(graph_header));

        if (!m_git_graph_collapsed)
        {
            for (std::size_t i = 0; i < m_history.size(); ++i)
            {
                const auto& commit = m_history[i];
                SourceControlRow row{};
                row.kind = SourceControlRowKind::GitGraphCommit;
                row.file_index = i;
                row.label = commit.message;
                row.commit_author = commit.author_name;
                row.commit_sha = commit.sha;
                row.commit_short_sha = commit.short_sha;
                row.is_head = (i == 0);
                if (i == 0)
                {
                    row.commit_branch = m_status.active_branch.empty() ? "main" : m_status.active_branch;
                }
                m_visible_rows.push_back(std::move(row));
            }
        }
    }
}

bool WorkspaceSourceControlModel::scroll(std::ptrdiff_t delta, std::size_t viewport_rows) noexcept
{
    if (m_visible_rows.empty() || viewport_rows >= m_visible_rows.size())
    {
        if (m_scroll_offset != 0)
        {
            m_scroll_offset = 0;
            return true;
        }
        return false;
    }

    const std::size_t max_offset = m_visible_rows.size() - viewport_rows;
    const std::size_t prev = m_scroll_offset;

    if (delta > 0)
    {
        m_scroll_offset = (m_scroll_offset >= static_cast<std::size_t>(delta))
                              ? m_scroll_offset - static_cast<std::size_t>(delta)
                              : 0;
    }
    else if (delta < 0)
    {
        const std::size_t abs_delta = static_cast<std::size_t>(-delta);
        m_scroll_offset = std::min(m_scroll_offset + abs_delta, max_offset);
    }

    return m_scroll_offset != prev;
}

void WorkspaceSourceControlModel::set_commit_message(std::string_view msg)
{
    m_commit_message = msg;
    m_caret = std::min(m_caret, m_commit_message.size());
    m_selection_anchor.reset();
}

std::pair<std::size_t, std::size_t> WorkspaceSourceControlModel::get_selection_range() const noexcept
{
    if (!m_selection_anchor.has_value() || *m_selection_anchor == m_caret)
    {
        return {m_caret, m_caret};
    }
    return {std::min(*m_selection_anchor, m_caret), std::max(*m_selection_anchor, m_caret)};
}

std::string WorkspaceSourceControlModel::get_selected_text() const
{
    const auto [min_pos, max_pos] = get_selection_range();
    if (min_pos == max_pos || max_pos > m_commit_message.size()) return {};
    return m_commit_message.substr(min_pos, max_pos - min_pos);
}

void WorkspaceSourceControlModel::set_caret_and_selection(std::size_t anchor, std::size_t caret)
{
    m_caret = std::min(caret, m_commit_message.size());
    m_selection_anchor = std::min(anchor, m_commit_message.size());
}

void WorkspaceSourceControlModel::insert_text(std::string_view text)
{
    if (has_selection())
    {
        const auto [min_pos, max_pos] = get_selection_range();
        m_commit_message.erase(min_pos, max_pos - min_pos);
        m_caret = min_pos;
        m_selection_anchor.reset();
    }

    m_commit_message.insert(m_caret, text);
    m_caret += text.size();
    m_selection_anchor.reset();
}

void WorkspaceSourceControlModel::handle_backspace()
{
    if (has_selection())
    {
        const auto [min_pos, max_pos] = get_selection_range();
        m_commit_message.erase(min_pos, max_pos - min_pos);
        m_caret = min_pos;
        m_selection_anchor.reset();
        return;
    }

    if (m_caret > 0 && !m_commit_message.empty())
    {
        std::size_t erase_len = 1;
        while (m_caret >= erase_len + 1 &&
               (static_cast<unsigned char>(m_commit_message[m_caret - erase_len]) & 0xC0U) == 0x80U)
        {
            erase_len++;
        }
        m_commit_message.erase(m_caret - erase_len, erase_len);
        m_caret -= erase_len;
    }
}

void WorkspaceSourceControlModel::handle_delete()
{
    if (has_selection())
    {
        const auto [min_pos, max_pos] = get_selection_range();
        m_commit_message.erase(min_pos, max_pos - min_pos);
        m_caret = min_pos;
        m_selection_anchor.reset();
        return;
    }

    if (m_caret < m_commit_message.size())
    {
        std::size_t erase_len = 1;
        while (m_caret + erase_len < m_commit_message.size() &&
               (static_cast<unsigned char>(m_commit_message[m_caret + erase_len]) & 0xC0U) == 0x80U)
        {
            erase_len++;
        }
        m_commit_message.erase(m_caret, erase_len);
    }
}

void WorkspaceSourceControlModel::handle_left(bool extend_selection)
{
    if (extend_selection)
    {
        if (!m_selection_anchor.has_value())
        {
            m_selection_anchor = m_caret;
        }
        if (m_caret > 0) m_caret--;
    }
    else
    {
        if (has_selection())
        {
            m_caret = get_selection_range().first;
            m_selection_anchor.reset();
        }
        else if (m_caret > 0)
        {
            m_caret--;
        }
    }
}

void WorkspaceSourceControlModel::handle_right(bool extend_selection)
{
    if (extend_selection)
    {
        if (!m_selection_anchor.has_value())
        {
            m_selection_anchor = m_caret;
        }
        if (m_caret < m_commit_message.size()) m_caret++;
    }
    else
    {
        if (has_selection())
        {
            m_caret = get_selection_range().second;
            m_selection_anchor.reset();
        }
        else if (m_caret < m_commit_message.size())
        {
            m_caret++;
        }
    }
}

void WorkspaceSourceControlModel::handle_home(bool extend_selection)
{
    if (extend_selection)
    {
        if (!m_selection_anchor.has_value()) m_selection_anchor = m_caret;
    }
    else
    {
        m_selection_anchor.reset();
    }
    m_caret = 0;
}

void WorkspaceSourceControlModel::handle_end(bool extend_selection)
{
    if (extend_selection)
    {
        if (!m_selection_anchor.has_value()) m_selection_anchor = m_caret;
    }
    else
    {
        m_selection_anchor.reset();
    }
    m_caret = m_commit_message.size();
}

void WorkspaceSourceControlModel::select_all()
{
    m_selection_anchor = 0;
    m_caret = m_commit_message.size();
}

bool WorkspaceSourceControlModel::commit()
{
    if (m_commit_message.empty())
    {
        m_last_error = "Please enter a commit message";
        return false;
    }

    // Auto-stage all if nothing is staged
    if (m_status.staged_items.empty())
    {
        if (!stage_all())
        {
            return false;
        }
    }

    if (!m_repo.commit(m_commit_message))
    {
        m_last_error = m_repo.get_last_error();
        return false;
    }

    m_commit_message.clear();
    m_caret = 0;
    m_selection_anchor.reset();
    m_last_error.clear();
    refresh_status();
    return true;
}

bool WorkspaceSourceControlModel::stage_file(const std::filesystem::path& path)
{
    const bool ok = m_repo.stage_file(path);
    if (!ok) m_last_error = m_repo.get_last_error();
    refresh_status();
    return ok;
}

bool WorkspaceSourceControlModel::unstage_file(const std::filesystem::path& path)
{
    const bool ok = m_repo.unstage_file(path);
    if (!ok) m_last_error = m_repo.get_last_error();
    refresh_status();
    return ok;
}

bool WorkspaceSourceControlModel::stage_all()
{
    const bool ok = m_repo.stage_all();
    if (!ok) m_last_error = m_repo.get_last_error();
    refresh_status();
    return ok;
}

bool WorkspaceSourceControlModel::unstage_all()
{
    const bool ok = m_repo.unstage_all();
    if (!ok) m_last_error = m_repo.get_last_error();
    refresh_status();
    return ok;
}

bool WorkspaceSourceControlModel::discard_changes(const std::filesystem::path& path)
{
    bool ok = false;
    // If empty path, discard all changes
    if (path.empty())
    {
        for (const auto& item : m_status.unstaged_items)
        {
            m_repo.discard_file_changes(item.path);
        }
        for (const auto& item : m_status.untracked_items)
        {
            m_repo.clean_untracked_file(item.path);
        }
        refresh_status();
        return true;
    }

    // If it's untracked, clean file; otherwise checkout head
    const auto abs_path = m_workspace_root / path;
    if (std::filesystem::exists(abs_path))
    {
        ok = m_repo.discard_file_changes(path);
        if (!ok)
        {
            ok = m_repo.clean_untracked_file(path);
        }
    }
    else
    {
        ok = m_repo.discard_file_changes(path);
    }

    if (!ok) m_last_error = m_repo.get_last_error();
    refresh_status();
    return ok;
}

std::optional<std::filesystem::path> WorkspaceSourceControlModel::activate_visible_row(std::size_t visible_index)
{
    if (visible_index >= m_visible_rows.size()) return std::nullopt;
    const auto& row = m_visible_rows[visible_index];

    if (row.kind == SourceControlRowKind::RepositoryHeader)
    {
        toggle_repo_collapsed();
        return std::nullopt;
    }
    if (row.kind == SourceControlRowKind::SectionHeaderStaged)
    {
        toggle_staged_collapsed();
        return std::nullopt;
    }
    if (row.kind == SourceControlRowKind::SectionHeaderUnstaged)
    {
        toggle_unstaged_collapsed();
        return std::nullopt;
    }
    if (row.kind == SourceControlRowKind::GitGraphHeader)
    {
        toggle_git_graph_collapsed();
        return std::nullopt;
    }
    if (row.kind == SourceControlRowKind::GitGraphCommit)
    {
        return std::nullopt;
    }

    // Return absolute file path to open in editor
    return m_workspace_root / row.path;
}

} // namespace Zenvra::UI::Editor
