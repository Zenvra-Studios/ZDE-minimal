#pragma once

#include "Services/Project/ProjectDetector.h"
#include "Services/Project/ProjectInfo.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Project
{

class ProjectService
{
public:
    static ProjectService& instance();

    void set_workspace(
        const std::filesystem::path& workspace_root,
        const std::filesystem::path& active_file = {});

    void refresh();

    [[nodiscard]] const ProjectInfo& get_project_info() const;
    [[nodiscard]] const std::vector<ProjectTarget>& get_targets() const;
    [[nodiscard]] const ProjectTarget* get_active_target() const;
    bool set_active_target(std::string_view target_id);

private:
    ProjectService() = default;
    mutable std::mutex m_mutex;
    std::filesystem::path m_workspace_root;
    std::filesystem::path m_active_file;
    ProjectInfo m_project_info;
    std::string m_active_target_id;
};

} // namespace Zenvra::Services::Project
