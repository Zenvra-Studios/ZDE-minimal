#include "Services/Build/GradleBuildSystem.h"

namespace Zenvra::Services::Build
{

bool GradleBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::exists(root / "build.gradle", ec) ||
           std::filesystem::exists(root / "build.gradle.kts", ec);
}

BuildResult GradleBuildSystem::configure(const std::filesystem::path& /*root*/, const std::string& /*preset*/)
{
    BuildResult result;
    result.success = true;
    result.exit_code = 0;
    return result;
}

BuildResult GradleBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& /*preset*/,
    const std::string& /*config*/)
{
    BuildResult result;
    Process::ProcessSpec spec;

    std::error_code ec;
#if defined(_WIN32)
    if (std::filesystem::exists(root / "gradlew.bat", ec))
    {
        spec.executable = (root / "gradlew.bat").string();
    }
    else
    {
        spec.executable = "gradle";
    }
#else
    if (std::filesystem::exists(root / "gradlew", ec))
    {
        spec.executable = (root / "gradlew").string();
    }
    else
    {
        spec.executable = "gradle";
    }
#endif

    if (!target.empty())
    {
        spec.arguments.push_back(target);
    }
    else
    {
        spec.arguments.push_back("build");
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

BuildResult GradleBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    Process::ProcessSpec spec;
    std::error_code ec;
#if defined(_WIN32)
    if (std::filesystem::exists(root / "gradlew.bat", ec))
    {
        spec.executable = (root / "gradlew.bat").string();
    }
    else
    {
        spec.executable = "gradle";
    }
#else
    if (std::filesystem::exists(root / "gradlew", ec))
    {
        spec.executable = (root / "gradlew").string();
    }
    else
    {
        spec.executable = "gradle";
    }
#endif
    spec.arguments.push_back("clean");
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

std::vector<BuildTarget> GradleBuildSystem::get_targets(const std::filesystem::path& /*root*/)
{
    return {
        BuildTarget{.name = "build", .output_artifact = "build/libs", .is_default = true},
        BuildTarget{.name = "classes", .output_artifact = "build/classes", .is_default = false}
    };
}

} // namespace Zenvra::Services::Build
