#include "Services/Build/MakeBuildSystem.h"

namespace Zenvra::Services::Build
{

bool MakeBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::exists(root / "Makefile", ec) ||
           std::filesystem::exists(root / "makefile", ec) ||
           std::filesystem::exists(root / "GNUmakefile", ec);
}

BuildResult MakeBuildSystem::configure(const std::filesystem::path& /*root*/, const std::string& /*preset*/)
{
    BuildResult result;
    result.success = true;
    result.exit_code = 0;
    return result;
}

BuildResult MakeBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& /*preset*/,
    const std::string& /*config*/)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "make";
    if (!target.empty())
    {
        spec.arguments.push_back(target);
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

BuildResult MakeBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "make";
    spec.arguments.push_back("clean");
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

std::vector<BuildTarget> MakeBuildSystem::get_targets(const std::filesystem::path& /*root*/)
{
    return {
        BuildTarget{.name = "all", .output_artifact = "", .is_default = true}
    };
}

} // namespace Zenvra::Services::Build
