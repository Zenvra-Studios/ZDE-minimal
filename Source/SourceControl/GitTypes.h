#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Git
{

enum class GitFileStatus
{
    Unmodified,
    Untracked,
    Modified,
    Added,
    Deleted,
    Renamed,
    TypeChange,
    Ignored,
    Conflicted
};

[[nodiscard]] constexpr std::string_view git_file_status_to_string(GitFileStatus status) noexcept
{
    switch (status)
    {
    case GitFileStatus::Untracked: return "Untracked";
    case GitFileStatus::Modified: return "Modified";
    case GitFileStatus::Added: return "Added";
    case GitFileStatus::Deleted: return "Deleted";
    case GitFileStatus::Renamed: return "Renamed";
    case GitFileStatus::TypeChange: return "Type Change";
    case GitFileStatus::Ignored: return "Ignored";
    case GitFileStatus::Conflicted: return "Conflicted";
    case GitFileStatus::Unmodified:
    default:
        return "Unmodified";
    }
}

[[nodiscard]] constexpr std::string_view git_file_status_letter(GitFileStatus status) noexcept
{
    switch (status)
    {
    case GitFileStatus::Untracked: return "U";
    case GitFileStatus::Modified: return "M";
    case GitFileStatus::Added: return "A";
    case GitFileStatus::Deleted: return "D";
    case GitFileStatus::Renamed: return "R";
    case GitFileStatus::TypeChange: return "T";
    case GitFileStatus::Ignored: return "I";
    case GitFileStatus::Conflicted: return "!";
    case GitFileStatus::Unmodified:
    default:
        return "";
    }
}

struct GitStatusItem
{
    std::filesystem::path path;
    std::filesystem::path old_path;
    GitFileStatus status = GitFileStatus::Unmodified;
    bool is_staged = false;
    std::size_t additions = 0;
    std::size_t deletions = 0;
};

struct GitBranchInfo
{
    std::string name;
    bool is_head = false;
    bool is_remote = false;
    std::string upstream;
};

struct GitCommitInfo
{
    std::string sha;
    std::string short_sha;
    std::string message;
    std::string author_name;
    std::string author_email;
    std::int64_t timestamp_unix = 0;
    std::vector<std::string> parent_shas;
};

struct GitDiffHunk
{
    int old_start = 0;
    int old_lines = 0;
    int new_start = 0;
    int new_lines = 0;
    std::string header;
    std::string content;
};

struct GitDiffFile
{
    std::filesystem::path path;
    std::vector<GitDiffHunk> hunks;
    std::size_t additions = 0;
    std::size_t deletions = 0;
};

struct GitRepositoryStatus
{
    std::string active_branch;
    std::string head_commit_sha;
    std::string head_commit_summary;
    std::size_t ahead_count = 0;
    std::size_t behind_count = 0;
    std::vector<GitStatusItem> staged_items;
    std::vector<GitStatusItem> unstaged_items;
    std::vector<GitStatusItem> untracked_items;
};

} // namespace Zenvra::Git
