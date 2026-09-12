#pragma once

#include "UI/Toolbar/ToolbarTypes.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Tools::Classification
{

struct ExecutionCommand
{
    std::string program;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

class ProjectToolClassifier
{
public:
    [[nodiscard]] static std::vector<UI::Toolbar::BinaryTargetProfile> detect_configurations(
        const std::filesystem::path& workspace_root,
        const std::filesystem::path& active_file = {});

    [[nodiscard]] static std::optional<UI::Toolbar::ToolClassification> classify_file(
        const std::filesystem::path& file_path);

    [[nodiscard]] static UI::Toolbar::BinaryTargetProfile create_profile_for_file(
        const std::filesystem::path& file_path);

    [[nodiscard]] static std::string detect_cmake_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_cargo_package_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_java_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_python_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string detect_pascal_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string extract_project_name(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static ExecutionCommand create_run_command(
        const UI::Toolbar::BinaryTargetProfile& profile,
        const std::filesystem::path& workspace_root,
        const std::string& preset = "",
        const std::string& config = "Debug");

    [[nodiscard]] static ExecutionCommand create_debug_command(
        const UI::Toolbar::BinaryTargetProfile& profile,
        const std::filesystem::path& workspace_root,
        const std::string& preset = "",
        const std::string& config = "Debug");
};

} // namespace Zenvra::Tools::Classification
