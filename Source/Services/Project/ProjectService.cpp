#include "Services/Project/ProjectService.h"

namespace Zenvra::Services::Project
{

ProjectService& ProjectService::instance()
{
    static ProjectService s_instance;
    return s_instance;
}

void ProjectService::set_workspace(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& active_file)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workspace_root = workspace_root;
    m_active_file = active_file;
    m_project_info = ProjectDetector::detect(m_workspace_root, m_active_file);
    if (!m_project_info.targets.empty())
    {
        m_active_target_id = m_project_info.targets.front().id;
    }
    else
    {
        m_active_target_id.clear();
    }
}

void ProjectService::refresh()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_project_info = ProjectDetector::detect(m_workspace_root, m_active_file);
    if (!m_project_info.targets.empty())
    {
        const bool active_still_exists = std::any_of(
            m_project_info.targets.begin(), m_project_info.targets.end(),
            [this](const auto& t) { return t.id == m_active_target_id; });
        if (!active_still_exists)
        {
            m_active_target_id = m_project_info.targets.front().id;
        }
    }
}

const ProjectInfo& ProjectService::get_project_info() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_project_info;
}

const std::vector<ProjectTarget>& ProjectService::get_targets() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_project_info.targets;
}

const ProjectTarget* ProjectService::get_active_target() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& t : m_project_info.targets)
    {
        if (t.id == m_active_target_id)
        {
            return &t;
        }
    }
    if (!m_project_info.targets.empty())
    {
        return &m_project_info.targets.front();
    }
    return nullptr;
}

bool ProjectService::set_active_target(std::string_view target_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& t : m_project_info.targets)
    {
        if (t.id == target_id)
        {
            m_active_target_id = std::string(target_id);
            return true;
        }
    }
    return false;
}

} // namespace Zenvra::Services::Project
