#include "Services/Build/MavenBuildSystem.h"

namespace Zenvra::Services::Build
{

bool MavenBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::exists(root / "pom.xml", ec);
}

BuildResult MavenBuildSystem::configure(const std::filesystem::path& /*root*/, const std::string& /*preset*/)
{
    BuildResult result;
    result.success = true;
    result.exit_code = 0;
    return result;
}

BuildResult MavenBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& /*preset*/,
    const std::string& /*config*/)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "mvn";
    if (!target.empty())
    {
        spec.arguments.push_back(target);
    }
    else
    {
        spec.arguments.push_back("compile");
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

BuildResult MavenBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "mvn";
    spec.arguments.push_back("clean");
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

std::vector<BuildTarget> MavenBuildSystem::get_targets(const std::filesystem::path& /*root*/)
{
    return {
        BuildTarget{.name = "compile", .output_artifact = "target/classes", .is_default = true},
        BuildTarget{.name = "package", .output_artifact = "target", .is_default = false}
    };
}

} // namespace Zenvra::Services::Build
