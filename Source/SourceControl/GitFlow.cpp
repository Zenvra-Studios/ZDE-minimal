#include "SourceControl/GitFlow.h"

namespace Zenvra::Git
{

bool GitFlow::start_feature(std::string_view feature_name)
{
    if (feature_name.empty()) return false;
    const std::string full_name = get_feature_prefix() + std::string(feature_name);

    // If develop branch exists, checkout develop first
    const auto branches = m_repo.list_branches();
    for (const auto& b : branches)
    {
        if (b.name == get_develop_branch())
        {
            (void)m_repo.checkout_branch(get_develop_branch());
            break;
        }
    }

    if (!m_repo.create_branch(full_name))
    {
        return false;
    }
    return m_repo.checkout_branch(full_name);
}

bool GitFlow::finish_feature(std::string_view feature_name)
{
    if (feature_name.empty()) return false;
    const std::string full_name = get_feature_prefix() + std::string(feature_name);

    if (!m_repo.checkout_branch(get_develop_branch()))
    {
        // Fallback to main
        if (!m_repo.checkout_branch(get_main_branch()))
        {
            return false;
        }
    }

    // Branch can be deleted or merged
    return m_repo.delete_branch(full_name, false);
}

bool GitFlow::start_release(std::string_view version)
{
    if (version.empty()) return false;
    const std::string full_name = get_release_prefix() + std::string(version);

    (void)m_repo.checkout_branch(get_develop_branch());
    if (!m_repo.create_branch(full_name))
    {
        return false;
    }
    return m_repo.checkout_branch(full_name);
}

bool GitFlow::finish_release(std::string_view version)
{
    if (version.empty()) return false;
    const std::string full_name = get_release_prefix() + std::string(version);

    if (m_repo.checkout_branch(get_main_branch()))
    {
        return m_repo.delete_branch(full_name, false);
    }
    return false;
}

bool GitFlow::start_hotfix(std::string_view hotfix_name)
{
    if (hotfix_name.empty()) return false;
    const std::string full_name = get_hotfix_prefix() + std::string(hotfix_name);

    (void)m_repo.checkout_branch(get_main_branch());
    if (!m_repo.create_branch(full_name))
    {
        return false;
    }
    return m_repo.checkout_branch(full_name);
}

bool GitFlow::finish_hotfix(std::string_view hotfix_name)
{
    if (hotfix_name.empty()) return false;
    const std::string full_name = get_hotfix_prefix() + std::string(hotfix_name);

    if (m_repo.checkout_branch(get_main_branch()))
    {
        return m_repo.delete_branch(full_name, false);
    }
    return false;
}

} // namespace Zenvra::Git
