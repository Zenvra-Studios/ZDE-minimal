#pragma once

#include "SourceControl/GitRepository.h"

#include <string>
#include <string_view>

namespace Zenvra::Git
{

class GitFlow
{
public:
    explicit GitFlow(GitRepository& repo) : m_repo(repo) {}

    [[nodiscard]] bool start_feature(std::string_view feature_name);
    [[nodiscard]] bool finish_feature(std::string_view feature_name);

    [[nodiscard]] bool start_release(std::string_view version);
    [[nodiscard]] bool finish_release(std::string_view version);

    [[nodiscard]] bool start_hotfix(std::string_view hotfix_name);
    [[nodiscard]] bool finish_hotfix(std::string_view hotfix_name);

    [[nodiscard]] std::string get_feature_prefix() const { return "feature/"; }
    [[nodiscard]] std::string get_release_prefix() const { return "release/"; }
    [[nodiscard]] std::string get_hotfix_prefix() const { return "hotfix/"; }
    [[nodiscard]] std::string get_main_branch() const { return "main"; }
    [[nodiscard]] std::string get_develop_branch() const { return "develop"; }

private:
    GitRepository& m_repo;
};

} // namespace Zenvra::Git
