#pragma once

#include "Services/Project/ProjectInfo.h"

#include <filesystem>
#include <vector>

namespace Zenvra::Services::Project
{

class ProjectDetector
{
public:
    [[nodiscard]] static ProjectInfo detect(
        const std::filesystem::path& workspace_root,
        const std::filesystem::path& active_file = {});

    [[nodiscard]] static std::string detect_cmake_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::vector<std::string> detect_cmake_executable_targets(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_cargo_package_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::vector<std::string> detect_cargo_binary_targets(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_python_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::vector<ProjectTarget> detect_python_targets(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::optional<std::filesystem::path> detect_virtual_env(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_java_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_pascal_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string resolve_python_interpreter(
        const std::filesystem::path& workspace_root);
};

} // namespace Zenvra::Services::Project
