#pragma once

#include "SourceControl/GitTypes.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct git_repository;

namespace Zenvra::Git
{

class GitRepository
{
public:
    GitRepository();
    ~GitRepository();

    GitRepository(const GitRepository&) = delete;
    GitRepository& operator=(const GitRepository&) = delete;
    GitRepository(GitRepository&& other) noexcept;
    GitRepository& operator=(GitRepository&& other) noexcept;

    static void global_init();
    static void global_shutdown();

    [[nodiscard]] bool open(const std::filesystem::path& workspace_root);
    [[nodiscard]] bool init_repository(const std::filesystem::path& workspace_root);
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::filesystem::path get_workdir() const;
    [[nodiscard]] std::filesystem::path get_git_dir() const;

    [[nodiscard]] GitRepositoryStatus get_status();

    [[nodiscard]] bool stage_file(const std::filesystem::path& relative_path);
    [[nodiscard]] bool unstage_file(const std::filesystem::path& relative_path);
    [[nodiscard]] bool stage_all();
    [[nodiscard]] bool unstage_all();
    [[nodiscard]] bool discard_file_changes(const std::filesystem::path& relative_path);
    [[nodiscard]] bool clean_untracked_file(const std::filesystem::path& relative_path);

    [[nodiscard]] bool commit(
        std::string_view message,
        std::string_view author_name = "",
        std::string_view author_email = "");

    [[nodiscard]] std::string get_active_branch();
    [[nodiscard]] std::vector<GitBranchInfo> list_branches();
    [[nodiscard]] bool create_branch(std::string_view branch_name);
    [[nodiscard]] bool checkout_branch(std::string_view branch_name);
    [[nodiscard]] bool delete_branch(std::string_view branch_name, bool force = false);

    [[nodiscard]] std::vector<GitCommitInfo> get_history(std::size_t max_count = 50);
    [[nodiscard]] GitDiffFile get_file_diff(const std::filesystem::path& relative_path, bool staged);
    [[nodiscard]] std::string get_file_diff_unified(const std::filesystem::path& relative_path, bool staged);

    [[nodiscard]] std::string get_last_error() const;

private:
    void set_error(std::string_view err) const;

    mutable std::mutex m_mutex;
    git_repository* m_repo = nullptr;
    std::filesystem::path m_workdir;
    mutable std::string m_last_error;
};

} // namespace Zenvra::Git
