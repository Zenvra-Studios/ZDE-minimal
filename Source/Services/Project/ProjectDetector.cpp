#include "Services/Project/ProjectDetector.h"
#include "Tools/Classification/ProjectToolClassifier.h"

#include <algorithm>

namespace Zenvra::Services::Project
{

namespace
{

ProjectType to_service_type(UI::Toolbar::ToolClassification classification)
{
    switch (classification)
    {
    case UI::Toolbar::ToolClassification::CMake: return ProjectType::CMake;
    case UI::Toolbar::ToolClassification::TomlCargo: return ProjectType::Cargo;
    case UI::Toolbar::ToolClassification::Python: return ProjectType::Python;
    case UI::Toolbar::ToolClassification::Java: return ProjectType::PlainJava;
    case UI::Toolbar::ToolClassification::Pascal: return ProjectType::Pascal;
    case UI::Toolbar::ToolClassification::CustomExecutable: return ProjectType::Unknown;
    }
    return ProjectType::Unknown;
}

ExecutionModel to_execution_model(ProjectType type)
{
    switch (type)
    {
    case ProjectType::Python:
        return ExecutionModel::InterpreterScript;
    case ProjectType::CMake:
    case ProjectType::Cargo:
    case ProjectType::Pascal:
    case ProjectType::PlainCpp:
    case ProjectType::PlainRust:
        return ExecutionModel::CompiledBinary;
    case ProjectType::Maven:
    case ProjectType::Gradle:
    case ProjectType::PlainJava:
        return ExecutionModel::CustomCommand;
    default:
        return ExecutionModel::CompiledBinary;
    }
}

} // namespace

std::string ProjectDetector::detect_cmake_project_name(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_cmake_project_name(workspace_root);
}

std::vector<std::string> ProjectDetector::detect_cmake_executable_targets(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_cmake_executable_targets(workspace_root);
}

std::string ProjectDetector::detect_cargo_package_name(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_cargo_package_name(workspace_root);
}

std::vector<std::string> ProjectDetector::detect_cargo_binary_targets(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_cargo_binary_targets(workspace_root);
}

std::string ProjectDetector::detect_python_project_name(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_python_project_name(workspace_root);
}

std::vector<ProjectTarget> ProjectDetector::detect_python_targets(const std::filesystem::path& workspace_root)
{
    std::vector<ProjectTarget> targets;
    const auto py_profiles = Tools::Classification::ProjectToolClassifier::detect_python_targets(workspace_root);
    for (const auto& p : py_profiles)
    {
        targets.push_back(ProjectTarget{
            .id = p.id,
            .name = p.name,
            .artifact_or_script = p.executable_path,
            .project_type = ProjectType::Python,
            .execution_model = ExecutionModel::InterpreterScript,
            .icon_asset = p.icon_asset,
            .is_default = p.is_default
        });
    }
    return targets;
}

std::optional<std::filesystem::path> ProjectDetector::detect_virtual_env(const std::filesystem::path& workspace_root)
{
    std::error_code ec;
    for (const auto& venv_name : {".venv", "venv", "env", ".env"})
    {
        const auto candidate = workspace_root / venv_name;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec))
        {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string ProjectDetector::resolve_python_interpreter(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::resolve_python_interpreter(workspace_root);
}

std::string ProjectDetector::detect_java_project_name(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_java_project_name(workspace_root);
}

std::string ProjectDetector::detect_pascal_project_name(const std::filesystem::path& workspace_root)
{
    return Tools::Classification::ProjectToolClassifier::detect_pascal_project_name(workspace_root);
}

ProjectInfo ProjectDetector::detect(
    const std::filesystem::path& workspace_root,
    const std::filesystem::path& active_file)
{
    ProjectInfo info;
    info.root = workspace_root;

    if (workspace_root.empty())
    {
        return info;
    }

    info.virtual_env = detect_virtual_env(workspace_root);

    const auto profiles = Tools::Classification::ProjectToolClassifier::detect_configurations(workspace_root, active_file);

    for (const auto& profile : profiles)
    {
        const auto ptype = to_service_type(profile.classification);
        if (std::find(info.detected_types.begin(), info.detected_types.end(), ptype) == info.detected_types.end() &&
            ptype != ProjectType::Unknown)
        {
            info.detected_types.push_back(ptype);
        }

        info.targets.push_back(ProjectTarget{
            .id = profile.id,
            .name = profile.name,
            .artifact_or_script = profile.executable_path,
            .project_type = ptype,
            .execution_model = to_execution_model(ptype),
            .icon_asset = profile.icon_asset,
            .is_default = profile.is_default
        });
    }

    if (!info.targets.empty())
    {
        info.primary_type = info.targets.front().project_type;
        info.project_name = info.targets.front().name;
    }
    else
    {
        info.project_name = workspace_root.filename().string();
        info.primary_type = ProjectType::Unknown;
    }

    // Set manifest if exists
    std::error_code ec;
    if (std::filesystem::exists(workspace_root / "CMakeLists.txt", ec))
    {
        info.manifest = workspace_root / "CMakeLists.txt";
        info.has_build_system = true;
    }
    else if (std::filesystem::exists(workspace_root / "Cargo.toml", ec))
    {
        info.manifest = workspace_root / "Cargo.toml";
        info.has_build_system = true;
    }
    else if (std::filesystem::exists(workspace_root / "pom.xml", ec))
    {
        info.manifest = workspace_root / "pom.xml";
        info.has_build_system = true;
    }
    else if (std::filesystem::exists(workspace_root / "build.gradle", ec) ||
             std::filesystem::exists(workspace_root / "build.gradle.kts", ec))
    {
        info.manifest = workspace_root / "build.gradle";
        info.has_build_system = true;
    }
    else if (std::filesystem::exists(workspace_root / "pyproject.toml", ec))
    {
        info.manifest = workspace_root / "pyproject.toml";
    }

    return info;
}

} // namespace Zenvra::Services::Project
