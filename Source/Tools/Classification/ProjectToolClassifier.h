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

    [[nodiscard]] static std::vector<std::string> detect_cmake_executable_targets(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::vector<std::string> detect_cargo_binary_targets(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::vector<UI::Toolbar::BinaryTargetProfile> detect_python_targets(
        const std::filesystem::path& workspace_root);

    struct CompiledBinary
    {
        std::string name;
        std::filesystem::path path;
        UI::Toolbar::ToolClassification classification = UI::Toolbar::ToolClassification::CustomExecutable;
        std::string architecture = "x86_64";
    };

    [[nodiscard]] static std::string detect_binary_architecture(
        const std::filesystem::path& binary_path);

    [[nodiscard]] static std::vector<CompiledBinary> detect_compiled_binaries(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static std::string resolve_python_interpreter(
        const std::filesystem::path& workspace_root);

    [[nodiscard]] static ExecutionCommand create_run_command(
        const UI::Toolbar::BinaryTargetProfile& profile,
        const std::filesystem::path& workspace_root,
        const std::string& preset = "",
        const std::string& config = "Debug",
        const std::string& arch = "x86_64");

    [[nodiscard]] static ExecutionCommand create_debug_command(
        const UI::Toolbar::BinaryTargetProfile& profile,
        const std::filesystem::path& workspace_root,
        const std::string& preset = "",
        const std::string& config = "Debug",
        const std::string& arch = "x86_64");
};

} // namespace Zenvra::Tools::Classification
