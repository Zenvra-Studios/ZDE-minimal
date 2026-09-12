#pragma once

#include "Services/Project/ProjectType.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Services::Project
{

struct ProjectTarget
{
    std::string id;
    std::string name;
    std::string artifact_or_script;
    ProjectType project_type = ProjectType::Unknown;
    ExecutionModel execution_model = ExecutionModel::CompiledBinary;
    std::string icon_asset;
    bool is_default = false;
};

struct ProjectInfo
{
    std::filesystem::path root;
    std::string project_name;
    ProjectType primary_type = ProjectType::Unknown;
    std::vector<ProjectType> detected_types;
    std::vector<ProjectTarget> targets;
    std::optional<std::filesystem::path> manifest;
    std::optional<std::filesystem::path> virtual_env;
    bool has_build_system = false;
};

} // namespace Zenvra::Services::Project
