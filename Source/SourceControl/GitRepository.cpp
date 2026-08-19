#include "SourceControl/GitRepository.h"

#if __has_include(<git2.h>)
#include <git2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>

namespace Zenvra::Git
{

namespace
{

struct GitErrorScope
{
    static std::string get()
    {
        const git_error* err = git_error_last();
        if (err && err->message)
        {
            return err->message;
        }
        return "Unknown git error";
    }
};

} // namespace

void GitRepository::global_init()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
        git_libgit2_init();
    });
}

void GitRepository::global_shutdown()
{
    git_libgit2_shutdown();
}

GitRepository::GitRepository()
{
    global_init();
}

GitRepository::~GitRepository()
{
    close();
}

GitRepository::GitRepository(GitRepository&& other) noexcept
{
    std::lock_guard<std::mutex> lock(other.m_mutex);
    m_repo = other.m_repo;
    m_workdir = std::move(other.m_workdir);
    m_last_error = std::move(other.m_last_error);
    other.m_repo = nullptr;
}

GitRepository& GitRepository::operator=(GitRepository&& other) noexcept
{
    if (this != &other)
    {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        close();
        m_repo = other.m_repo;
        m_workdir = std::move(other.m_workdir);
        m_last_error = std::move(other.m_last_error);
        other.m_repo = nullptr;
    }
    return *this;
}

void GitRepository::set_error(std::string_view err) const
{
    m_last_error = err;
}

std::string GitRepository::get_last_error() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last_error;
}

bool GitRepository::is_open() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_repo != nullptr;
}

void GitRepository::close() noexcept
{
    if (m_repo)
    {
        git_repository_free(m_repo);
        m_repo = nullptr;
    }
    m_workdir.clear();
}

bool GitRepository::open(const std::filesystem::path& workspace_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    close();

    if (workspace_root.empty() || !std::filesystem::exists(workspace_root))
    {
        set_error("Workspace path does not exist");
        return false;
    }

    git_repository* repo = nullptr;
    const std::string path_str = workspace_root.string();
    const int error = git_repository_open_ext(
        &repo,
        path_str.c_str(),
        0,
        nullptr);

    if (error < 0 || !repo)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    m_repo = repo;
    const char* workdir_cstr = git_repository_workdir(m_repo);
    if (workdir_cstr)
    {
        m_workdir = std::filesystem::path(workdir_cstr);
    }
    else
    {
        m_workdir = workspace_root;
    }

    return true;
}

bool GitRepository::init_repository(const std::filesystem::path& workspace_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    close();

    if (workspace_root.empty())
    {
        set_error("Empty workspace path for git init");
        return false;
    }

    git_repository* repo = nullptr;
    const std::string path_str = workspace_root.string();
    const int error = git_repository_init(&repo, path_str.c_str(), 0);

    if (error < 0 || !repo)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    m_repo = repo;
    const char* workdir_cstr = git_repository_workdir(m_repo);
    if (workdir_cstr)
    {
        m_workdir = std::filesystem::path(workdir_cstr);
    }
    else
    {
        m_workdir = workspace_root;
    }

    return true;
}

std::filesystem::path GitRepository::get_workdir() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_workdir;
}

std::filesystem::path GitRepository::get_git_dir() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return {};
    const char* p = git_repository_path(m_repo);
    return p ? std::filesystem::path(p) : std::filesystem::path{};
}

std::string GitRepository::get_active_branch()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return {};

    git_reference* head = nullptr;
    if (git_repository_head(&head, m_repo) < 0 || !head)
    {
        return "HEAD (empty)";
    }

    const char* branch_name = git_reference_shorthand(head);
    std::string name = branch_name ? branch_name : "HEAD";
    git_reference_free(head);
    return name;
}

GitRepositoryStatus GitRepository::get_status()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    GitRepositoryStatus result{};
    if (!m_repo) return result;

    // 1. Head branch and commit info
    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, m_repo) == 0 && head_ref)
    {
        const char* branch_name = git_reference_shorthand(head_ref);
        result.active_branch = branch_name ? branch_name : "HEAD";

        const git_oid* target_oid = git_reference_target(head_ref);
        if (target_oid)
        {
            char sha_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(sha_str, sizeof(sha_str), target_oid);
            result.head_commit_sha = sha_str;

            git_commit* commit_obj = nullptr;
            if (git_commit_lookup(&commit_obj, m_repo, target_oid) == 0 && commit_obj)
            {
                const char* summary = git_commit_summary(commit_obj);
                if (summary)
                {
                    result.head_commit_summary = summary;
                }
                git_commit_free(commit_obj);
            }
        }
        git_reference_free(head_ref);
    }
    else
    {
        result.active_branch = "HEAD (unborn)";
    }

    // 2. Status List options
    git_status_options opts{};
    git_status_options_init(&opts, GIT_STATUS_OPTIONS_VERSION);
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
                 GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR |
                 GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;

    git_status_list* status_list = nullptr;
    if (git_status_list_new(&status_list, m_repo, &opts) < 0 || !status_list)
    {
        set_error(GitErrorScope::get());
        return result;
    }

    const std::size_t count = git_status_list_entrycount(status_list);
    for (std::size_t i = 0; i < count; ++i)
    {
        const git_status_entry* entry = git_status_byindex(status_list, i);
        if (!entry) continue;

        // Check Staged Changes (Index flags)
        if (entry->status & (GIT_STATUS_INDEX_NEW |
                             GIT_STATUS_INDEX_MODIFIED |
                             GIT_STATUS_INDEX_DELETED |
                             GIT_STATUS_INDEX_RENAMED |
                             GIT_STATUS_INDEX_TYPECHANGE))
        {
            GitStatusItem item{};
            item.is_staged = true;

            if (entry->head_to_index && entry->head_to_index->new_file.path)
            {
                item.path = entry->head_to_index->new_file.path;
            }
            if (entry->head_to_index && entry->head_to_index->old_file.path)
            {
                item.old_path = entry->head_to_index->old_file.path;
            }

            if (entry->status & GIT_STATUS_INDEX_NEW)
                item.status = GitFileStatus::Added;
            else if (entry->status & GIT_STATUS_INDEX_MODIFIED)
                item.status = GitFileStatus::Modified;
            else if (entry->status & GIT_STATUS_INDEX_DELETED)
                item.status = GitFileStatus::Deleted;
            else if (entry->status & GIT_STATUS_INDEX_RENAMED)
                item.status = GitFileStatus::Renamed;
            else if (entry->status & GIT_STATUS_INDEX_TYPECHANGE)
                item.status = GitFileStatus::TypeChange;

            result.staged_items.push_back(std::move(item));
        }

        // Check Unstaged Changes (Workdir flags)
        if (entry->status & (GIT_STATUS_WT_MODIFIED |
                             GIT_STATUS_WT_DELETED |
                             GIT_STATUS_WT_RENAMED |
                             GIT_STATUS_WT_TYPECHANGE))
        {
            GitStatusItem item{};
            item.is_staged = false;

            if (entry->index_to_workdir && entry->index_to_workdir->new_file.path)
            {
                item.path = entry->index_to_workdir->new_file.path;
            }
            if (entry->index_to_workdir && entry->index_to_workdir->old_file.path)
            {
                item.old_path = entry->index_to_workdir->old_file.path;
            }

            if (entry->status & GIT_STATUS_WT_MODIFIED)
                item.status = GitFileStatus::Modified;
            else if (entry->status & GIT_STATUS_WT_DELETED)
                item.status = GitFileStatus::Deleted;
            else if (entry->status & GIT_STATUS_WT_RENAMED)
                item.status = GitFileStatus::Renamed;
            else if (entry->status & GIT_STATUS_WT_TYPECHANGE)
                item.status = GitFileStatus::TypeChange;

            result.unstaged_items.push_back(std::move(item));
        }

        // Check Untracked files
        if (entry->status & GIT_STATUS_WT_NEW)
        {
            GitStatusItem item{};
            item.is_staged = false;
            item.status = GitFileStatus::Untracked;
            if (entry->index_to_workdir && entry->index_to_workdir->new_file.path)
            {
                item.path = entry->index_to_workdir->new_file.path;
            }
            result.untracked_items.push_back(std::move(item));
        }
    }

    git_status_list_free(status_list);
    return result;
}

bool GitRepository::stage_file(const std::filesystem::path& relative_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;

    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) < 0 || !index)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    const std::string path_str = relative_path.generic_string();
    const auto abs_path = m_workdir / relative_path;

    int error = 0;
    if (!std::filesystem::exists(abs_path))
    {
        error = git_index_remove_bypath(index, path_str.c_str());
    }
    else
    {
        error = git_index_add_bypath(index, path_str.c_str());
    }

    if (error == 0)
    {
        error = git_index_write(index);
    }

    git_index_free(index);

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::unstage_file(const std::filesystem::path& relative_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;

    git_reference* head = nullptr;
    git_object* head_commit = nullptr;

    const bool has_head = (git_repository_head(&head, m_repo) == 0 && head != nullptr);
    if (has_head)
    {
        git_reference_peel(&head_commit, head, GIT_OBJECT_COMMIT);
        git_reference_free(head);
    }

    const std::string path_str = relative_path.generic_string();
    char* path_array[] = {const_cast<char*>(path_str.c_str())};
    git_strarray paths = {path_array, 1};

    const int error = git_reset_default(m_repo, head_commit, &paths);

    if (head_commit)
    {
        git_object_free(head_commit);
    }

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::stage_all()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;

    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) < 0 || !index)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    git_strarray paths = {nullptr, 0};
    int error = git_index_add_all(index, &paths, GIT_INDEX_ADD_DEFAULT | GIT_INDEX_ADD_DISABLE_PATHSPEC_MATCH, nullptr, nullptr);
    if (error == 0)
    {
        error = git_index_update_all(index, &paths, nullptr, nullptr);
    }
    if (error == 0)
    {
        error = git_index_write(index);
    }

    git_index_free(index);

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::unstage_all()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;

    git_reference* head = nullptr;
    git_object* head_commit = nullptr;

    if (git_repository_head(&head, m_repo) == 0 && head)
    {
        git_reference_peel(&head_commit, head, GIT_OBJECT_COMMIT);
        git_reference_free(head);
    }

    git_strarray paths = {nullptr, 0};
    const int error = git_reset_default(m_repo, head_commit, &paths);

    if (head_commit)
    {
        git_object_free(head_commit);
    }

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::discard_file_changes(const std::filesystem::path& relative_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;

    git_checkout_options opts{};
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    const std::string path_str = relative_path.generic_string();
    char* path_array[] = {const_cast<char*>(path_str.c_str())};
    opts.paths = {path_array, 1};

    const int error = git_checkout_head(m_repo, &opts);
    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::clean_untracked_file(const std::filesystem::path& relative_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_workdir.empty()) return false;

    const auto full_path = m_workdir / relative_path;
    std::error_code ec;
    if (std::filesystem::is_directory(full_path, ec))
    {
        return std::filesystem::remove_all(full_path, ec) > 0;
    }
    return std::filesystem::remove(full_path, ec);
}

bool GitRepository::commit(
    std::string_view message,
    std::string_view author_name,
    std::string_view author_email)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo) return false;
    if (message.empty())
    {
        set_error("Cannot create commit with empty message");
        return false;
    }

    git_index* index = nullptr;
    if (git_repository_index(&index, m_repo) < 0 || !index)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    git_oid tree_oid;
    if (git_index_write_tree(&tree_oid, index) < 0)
    {
        set_error(GitErrorScope::get());
        git_index_free(index);
        return false;
    }
    git_index_free(index);

    git_tree* tree = nullptr;
    if (git_tree_lookup(&tree, m_repo, &tree_oid) < 0 || !tree)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    // Determine parent commit (if any)
    git_commit* parent = nullptr;
    git_reference* head_ref = nullptr;
    std::size_t parent_count = 0;
    git_commit* parents[1] = {nullptr};

    if (git_repository_head(&head_ref, m_repo) == 0 && head_ref)
    {
        const git_oid* head_oid = git_reference_target(head_ref);
        if (head_oid && git_commit_lookup(&parent, m_repo, head_oid) == 0)
        {
            parents[0] = parent;
            parent_count = 1;
        }
        git_reference_free(head_ref);
    }

    // Author and committer signature
    git_signature* sig = nullptr;
    const std::string name = author_name.empty() ? "ZDE Developer" : std::string(author_name);
    const std::string email = author_email.empty() ? "developer@zenvra.dev" : std::string(author_email);

    if (git_signature_now(&sig, name.c_str(), email.c_str()) < 0 || !sig)
    {
        set_error(GitErrorScope::get());
        git_tree_free(tree);
        if (parent) git_commit_free(parent);
        return false;
    }

    git_oid new_commit_oid;
    const std::string msg_str(message);
    const int error = git_commit_create(
        &new_commit_oid,
        m_repo,
        "HEAD",
        sig,
        sig,
        nullptr,
        msg_str.c_str(),
        tree,
        parent_count,
        parent ? parents : nullptr);

    git_signature_free(sig);
    git_tree_free(tree);
    if (parent) git_commit_free(parent);

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    return true;
}

std::vector<GitBranchInfo> GitRepository::list_branches()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<GitBranchInfo> branches;
    if (!m_repo) return branches;

    git_branch_iterator* it = nullptr;
    if (git_branch_iterator_new(&it, m_repo, GIT_BRANCH_ALL) < 0 || !it)
    {
        return branches;
    }

    git_reference* ref = nullptr;
    git_branch_t type;
    while (git_branch_next(&ref, &type, it) == 0 && ref)
    {
        const char* bname = nullptr;
        if (git_branch_name(&bname, ref) == 0 && bname)
        {
            GitBranchInfo info{};
            info.name = bname;
            info.is_head = (git_branch_is_head(ref) == 1);
            info.is_remote = (type == GIT_BRANCH_REMOTE);

            git_reference* upstream_ref = nullptr;
            if (git_branch_upstream(&upstream_ref, ref) == 0 && upstream_ref)
            {
                const char* up_name = git_reference_shorthand(upstream_ref);
                if (up_name) info.upstream = up_name;
                git_reference_free(upstream_ref);
            }

            branches.push_back(std::move(info));
        }
        git_reference_free(ref);
    }

    git_branch_iterator_free(it);
    return branches;
}

bool GitRepository::create_branch(std::string_view branch_name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo || branch_name.empty()) return false;

    git_reference* head_ref = nullptr;
    if (git_repository_head(&head_ref, m_repo) < 0 || !head_ref)
    {
        set_error("Cannot create branch on empty repository without commits");
        return false;
    }

    git_commit* target_commit = nullptr;
    const git_oid* target_oid = git_reference_target(head_ref);
    if (!target_oid || git_commit_lookup(&target_commit, m_repo, target_oid) < 0 || !target_commit)
    {
        set_error(GitErrorScope::get());
        git_reference_free(head_ref);
        return false;
    }
    git_reference_free(head_ref);

    git_reference* new_branch = nullptr;
    const std::string name_str(branch_name);
    const int error = git_branch_create(&new_branch, m_repo, name_str.c_str(), target_commit, 0);

    git_commit_free(target_commit);
    if (new_branch) git_reference_free(new_branch);

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::checkout_branch(std::string_view branch_name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo || branch_name.empty()) return false;

    const std::string ref_name = "refs/heads/" + std::string(branch_name);
    git_checkout_options opts{};
    git_checkout_options_init(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;

    int error = git_repository_set_head(m_repo, ref_name.c_str());
    if (error == 0)
    {
        error = git_checkout_head(m_repo, &opts);
    }

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

bool GitRepository::delete_branch(std::string_view branch_name, bool force)
{
    (void)force;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_repo || branch_name.empty()) return false;

    git_reference* branch_ref = nullptr;
    const std::string name_str(branch_name);
    if (git_branch_lookup(&branch_ref, m_repo, name_str.c_str(), GIT_BRANCH_LOCAL) < 0 || !branch_ref)
    {
        set_error(GitErrorScope::get());
        return false;
    }

    const int error = git_branch_delete(branch_ref);
    git_reference_free(branch_ref);

    if (error < 0)
    {
        set_error(GitErrorScope::get());
        return false;
    }
    return true;
}

std::vector<GitCommitInfo> GitRepository::get_history(std::size_t max_count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<GitCommitInfo> list;
    if (!m_repo) return list;

    git_revwalk* walker = nullptr;
    if (git_revwalk_new(&walker, m_repo) < 0 || !walker)
    {
        return list;
    }

    git_revwalk_sorting(walker, GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL);
    if (git_revwalk_push_head(walker) < 0)
    {
        git_revwalk_free(walker);
        return list;
    }

    git_oid oid;
    std::size_t count = 0;
    while (git_revwalk_next(&oid, walker) == 0 && count < max_count)
    {
        git_commit* commit_obj = nullptr;
        if (git_commit_lookup(&commit_obj, m_repo, &oid) == 0 && commit_obj)
        {
            GitCommitInfo info{};
            char sha_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(sha_str, sizeof(sha_str), &oid);
            info.sha = sha_str;
            info.short_sha = info.sha.substr(0, 7);

            const char* msg = git_commit_summary(commit_obj);
            info.message = msg ? msg : "";

            const git_signature* author = git_commit_author(commit_obj);
            if (author)
            {
                info.author_name = author->name ? author->name : "";
                info.author_email = author->email ? author->email : "";
                info.timestamp_unix = author->when.time;
            }

            const unsigned int parent_cnt = git_commit_parentcount(commit_obj);
            for (unsigned int p = 0; p < parent_cnt; ++p)
            {
                const git_oid* p_oid = git_commit_parent_id(commit_obj, p);
                if (p_oid)
                {
                    char p_sha[GIT_OID_HEXSZ + 1];
                    git_oid_tostr(p_sha, sizeof(p_sha), p_oid);
                    info.parent_shas.emplace_back(p_sha);
                }
            }

            list.push_back(std::move(info));
            git_commit_free(commit_obj);
            ++count;
        }
    }

    git_revwalk_free(walker);
    return list;
}

GitDiffFile GitRepository::get_file_diff(const std::filesystem::path& relative_path, bool staged)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    GitDiffFile file_diff{};
    file_diff.path = relative_path;
    if (!m_repo) return file_diff;

    git_diff_options opts{};
    git_diff_options_init(&opts, GIT_DIFF_OPTIONS_VERSION);
    const std::string path_str = relative_path.generic_string();
    char* path_array[] = {const_cast<char*>(path_str.c_str())};
    opts.pathspec = {path_array, 1};

    git_diff* diff = nullptr;
    if (staged)
    {
        git_reference* head_ref = nullptr;
        git_tree* head_tree = nullptr;
        if (git_repository_head(&head_ref, m_repo) == 0 && head_ref)
        {
            git_commit* head_commit = nullptr;
            if (git_reference_peel(reinterpret_cast<git_object**>(&head_commit), head_ref, GIT_OBJECT_COMMIT) == 0 && head_commit)
            {
                git_commit_tree(&head_tree, head_commit);
                git_commit_free(head_commit);
            }
            git_reference_free(head_ref);
        }

        git_index* index = nullptr;
        git_repository_index(&index, m_repo);
        git_diff_tree_to_index(&diff, m_repo, head_tree, index, &opts);

        if (head_tree) git_tree_free(head_tree);
        if (index) git_index_free(index);
    }
    else
    {
        git_index* index = nullptr;
        git_repository_index(&index, m_repo);
        git_diff_index_to_workdir(&diff, m_repo, index, &opts);
        if (index) git_index_free(index);
    }

    if (!diff) return file_diff;

    struct HunkCollector
    {
        GitDiffFile* target;
        GitDiffHunk current_hunk;
        bool in_hunk = false;
    } collector{&file_diff, {}, false};

    auto hunk_cb = [](const git_diff_delta*, const git_diff_hunk* hunk, void* payload) -> int {
        auto* coll = static_cast<HunkCollector*>(payload);
        if (coll->in_hunk)
        {
            coll->target->hunks.push_back(std::move(coll->current_hunk));
        }
        coll->current_hunk = GitDiffHunk{};
        coll->current_hunk.old_start = hunk->old_start;
        coll->current_hunk.old_lines = hunk->old_lines;
        coll->current_hunk.new_start = hunk->new_start;
        coll->current_hunk.new_lines = hunk->new_lines;
        coll->current_hunk.header = std::string(hunk->header, hunk->header_len);
        coll->in_hunk = true;
        return 0;
    };

    auto line_cb = [](const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload) -> int {
        auto* coll = static_cast<HunkCollector*>(payload);
        if (coll->in_hunk)
        {
            coll->current_hunk.content.append(line->content, line->content_len);
            if (line->origin == GIT_DIFF_LINE_ADDITION)
            {
                coll->target->additions++;
            }
            else if (line->origin == GIT_DIFF_LINE_DELETION)
            {
                coll->target->deletions++;
            }
        }
        return 0;
    };

    git_diff_foreach(diff, nullptr, nullptr, hunk_cb, line_cb, &collector);

    if (collector.in_hunk)
    {
        file_diff.hunks.push_back(std::move(collector.current_hunk));
    }

    git_diff_free(diff);
    return file_diff;
}

std::string GitRepository::get_file_diff_unified(const std::filesystem::path& relative_path, bool staged)
{
    const GitDiffFile file_diff = get_file_diff(relative_path, staged);
    std::ostringstream ss;
    for (const auto& hunk : file_diff.hunks)
    {
        ss << hunk.header;
        ss << hunk.content;
    }
    return ss.str();
}

} // namespace Zenvra::Git

#else

namespace Zenvra::Git
{

void GitRepository::global_init() {}
void GitRepository::global_shutdown() {}

GitRepository::GitRepository() = default;
GitRepository::~GitRepository() = default;
GitRepository::GitRepository(GitRepository&& other) noexcept : m_workdir(std::move(other.m_workdir)), m_last_error(std::move(other.m_last_error)) {}
GitRepository& GitRepository::operator=(GitRepository&& other) noexcept {
    if (this != &other) {
        m_workdir = std::move(other.m_workdir);
        m_last_error = std::move(other.m_last_error);
    }
    return *this;
}

bool GitRepository::open(const std::filesystem::path& workspace_root) {
    m_workdir = workspace_root;
    return false;
}

bool GitRepository::init_repository(const std::filesystem::path& workspace_root) {
    m_workdir = workspace_root;
    return false;
}

void GitRepository::close() noexcept {
    m_repo = nullptr;
    m_workdir.clear();
}

bool GitRepository::is_open() const noexcept { return false; }
std::filesystem::path GitRepository::get_workdir() const { return m_workdir; }
std::filesystem::path GitRepository::get_git_dir() const { return m_workdir.empty() ? std::filesystem::path{} : m_workdir / ".git"; }

std::string GitRepository::get_last_error() const { return m_last_error; }
void GitRepository::set_error(std::string_view err) const { m_last_error = err; }

GitRepositoryStatus GitRepository::get_status() { return {}; }
bool GitRepository::stage_file(const std::filesystem::path&) { return false; }
bool GitRepository::unstage_file(const std::filesystem::path&) { return false; }
bool GitRepository::stage_all() { return false; }
bool GitRepository::unstage_all() { return false; }
bool GitRepository::discard_file_changes(const std::filesystem::path&) { return false; }
bool GitRepository::clean_untracked_file(const std::filesystem::path&) { return false; }
bool GitRepository::commit(std::string_view, std::string_view, std::string_view) { return false; }
std::string GitRepository::get_active_branch() { return ""; }
std::vector<GitBranchInfo> GitRepository::list_branches() { return {}; }
bool GitRepository::create_branch(std::string_view) { return false; }
bool GitRepository::checkout_branch(std::string_view) { return false; }
bool GitRepository::delete_branch(std::string_view, bool) { return false; }
std::vector<GitCommitInfo> GitRepository::get_history(std::size_t) { return {}; }
GitDiffFile GitRepository::get_file_diff(const std::filesystem::path&, bool) { return {}; }
std::string GitRepository::get_file_diff_unified(const std::filesystem::path&, bool) { return ""; }

} // namespace Zenvra::Git

#endif
