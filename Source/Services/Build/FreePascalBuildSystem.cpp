#include "Services/Build/FreePascalBuildSystem.h"

namespace Zenvra::Services::Build
{

bool FreePascalBuildSystem::detect(const std::filesystem::path& root)
{
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".lpr" || ext == ".pas" || ext == ".pp")
            {
                return true;
            }
        }
    }
    return false;
}

BuildResult FreePascalBuildSystem::configure(const std::filesystem::path& /*root*/, const std::string& /*preset*/)
{
    BuildResult result;
    result.success = true;
    result.exit_code = 0;
    return result;
}

BuildResult FreePascalBuildSystem::build(
    const std::filesystem::path& root,
    const std::string& target,
    const std::string& /*preset*/,
    const std::string& /*config*/)
{
    BuildResult result;
    Process::ProcessSpec spec;
    spec.executable = "fpc";
    spec.arguments.push_back("-B");

    std::string source_file = target;
    if (source_file.empty())
    {
        const auto targets = get_targets(root);
        if (!targets.empty())
        {
            source_file = targets.front().name;
        }
    }

    if (!source_file.empty())
    {
        spec.arguments.push_back(source_file);
    }
    spec.working_directory = root;

    const auto proc_res = m_runner.run(spec);
    result.success = proc_res.success;
    result.exit_code = proc_res.exit_code;
    result.stdout_output = proc_res.stdout_output;
    result.stderr_output = proc_res.stderr_output;

    if (!source_file.empty())
    {
        const std::filesystem::path p(source_file);
        const auto stem = p.stem().string();
#if defined(_WIN32)
        result.artifact_path = root / (stem + ".exe");
#else
        result.artifact_path = root / stem;
#endif
    }

    return result;
}

BuildResult FreePascalBuildSystem::clean(const std::filesystem::path& root)
{
    BuildResult result;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".o" || ext == ".ppu")
            {
                std::filesystem::remove(entry.path(), ec);
            }
        }
    }
    result.success = true;
    result.exit_code = 0;
    return result;
}

std::vector<BuildTarget> FreePascalBuildSystem::get_targets(const std::filesystem::path& root)
{
    std::vector<BuildTarget> targets;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec))
    {
        return targets;
    }

    // Prioritize .lpr, then .pas, then .pp
    for (const auto& entry : std::filesystem::directory_iterator(root, ec))
    {
        if (entry.is_regular_file())
        {
            const auto ext = entry.path().extension().string();
            if (ext == ".lpr")
            {
                const auto stem = entry.path().stem().string();
                targets.push_back(BuildTarget{
                    .name = entry.path().filename().string(),
                    .output_artifact = stem +
#if defined(_WIN32)
                        ".exe",
#else
                        "",
#endif
                    .is_default = targets.empty()
                });
            }
        }
    }

    if (targets.empty())
    {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (entry.is_regular_file())
            {
                const auto ext = entry.path().extension().string();
                if (ext == ".pas" || ext == ".pp")
                {
                    const auto stem = entry.path().stem().string();
                    targets.push_back(BuildTarget{
                        .name = entry.path().filename().string(),
                        .output_artifact = stem +
#if defined(_WIN32)
                            ".exe",
#else
                            "",
#endif
                        .is_default = targets.empty()
                    });
                }
            }
        }
    }

    return targets;
}

} // namespace Zenvra::Services::Build
