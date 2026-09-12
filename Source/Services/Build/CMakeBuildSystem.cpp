#include "Services/Build/CMakeBuildSystem.h"
#include "Services/Project/ProjectDetector.h"

namespace Zenvra::Services::Build
{

bool CMakeBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::exists(root / "CMakeLists.txt", ec);
}

BuildResult CMakeBuildSystem::configure(const std::filesystem::path& root, const std::string& preset)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "cmake";
    if (!preset.empty())
    {
        spec.arguments.push_back("--preset");
        spec.arguments.push_back(preset);
    }
    else
    {
        spec.arguments.push_back("-B");
        spec.arguments.push_back("build");
        spec.arguments.push_back("-S");
        spec.arguments.push_back(".");
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

BuildResult CMakeBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& preset,
    const std::string& config)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "cmake";
    spec.arguments.push_back("--build");
    if (!preset.empty())
    {
        spec.arguments.push_back("--preset");
        spec.arguments.push_back(preset);
    }
    else
    {
        spec.arguments.push_back("build");
        spec.arguments.push_back("--config");
        spec.arguments.push_back(config);
    }

    if (!target.empty())
    {
        spec.arguments.push_back("--target");
        spec.arguments.push_back(target);
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;

    // Resolve artifact
    if (!target.empty())
    {
#if defined(_WIN32)
        result.artifact_path = root / "build" / preset / "bin" / config / (target + ".exe");
#else
        result.artifact_path = root / "build" / preset / "bin" / config / target;
#endif
    }

    return result;
}

BuildResult CMakeBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "cmake";
    spec.arguments.push_back("--build");
    spec.arguments.push_back("build");
    spec.arguments.push_back("--target");
    spec.arguments.push_back("clean");
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    return result;
}

std::vector<BuildTarget> CMakeBuildSystem::get_targets(const std::filesystem::path& root)
{
    std::vector<BuildTarget> targets;
    const auto execs = Project::ProjectDetector::detect_cmake_executable_targets(root);
    for (const auto& ex : execs)
    {
        targets.push_back(BuildTarget{
            .name = ex,
            .output_artifact = ex +
#if defined(_WIN32)
                ".exe",
#else
                "",
#endif
            .is_default = targets.empty()
        });
    }
    return targets;
}

} // namespace Zenvra::Services::Build
