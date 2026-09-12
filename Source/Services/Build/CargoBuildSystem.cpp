#include "Services/Build/CargoBuildSystem.h"
#include "Services/Project/ProjectDetector.h"

namespace Zenvra::Services::Build
{

bool CargoBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::exists(root / "Cargo.toml", ec);
}

BuildResult CargoBuildSystem::configure(const std::filesystem::path& /*root*/, const std::string& /*preset*/)
{
    BuildResult result;
    result.success = true;
    result.exit_code = 0;
    return result;
}

BuildResult CargoBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& /*preset*/,
    const std::string& config)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "cargo";
    spec.arguments.push_back("build");

    if (config == "Release" || config == "release")
    {
        spec.arguments.push_back("--release");
    }

    if (!target.empty())
    {
        spec.arguments.push_back("--bin");
        spec.arguments.push_back(target);
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;

    std::string bin_name = target;
    if (bin_name.empty())
    {
        const auto targets = get_targets(root);
        if (!targets.empty())
        {
            bin_name = targets.front().name;
        }
    }

    if (!bin_name.empty())
    {
        const std::string profile_dir = (config == "Release" || config == "release") ? "release" : "debug";
#if defined(_WIN32)
        result.artifact_path = root / "target" / profile_dir / (bin_name + ".exe");
#else
        result.artifact_path = root / "target" / profile_dir / bin_name;
#endif
    }

    return result;
}

BuildResult CargoBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "cargo";
    spec.arguments.push_back("clean");
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;
    return result;
}

std::vector<BuildTarget> CargoBuildSystem::get_targets(const std::filesystem::path& root)
{
    std::vector<BuildTarget> targets;
    const auto binary_targets = Project::ProjectDetector::detect_cargo_binary_targets(root);
    for (const auto& bin : binary_targets)
    {
        targets.push_back(BuildTarget{
            .name = bin,
            .output_artifact = bin +
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
