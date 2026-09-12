#include "Services/Run/RunConfigurationResolver.h"
#include "Services/Project/ProjectDetector.h"

namespace Zenvra::Services::Run
{

ResolvedRun RunConfigurationResolver::resolve(
    const RunConfiguration& config,
    const std::filesystem::path& root,
    const std::string& preset)
{
    ResolvedRun resolved;
    std::error_code ec;

    const std::filesystem::path work_dir = config.working_directory.empty() ? root : config.working_directory;
    resolved.process_spec.working_directory = work_dir;
    for (const auto& [k, v] : config.environment_variables)
    {
        resolved.process_spec.environment.push_back(Process::EnvironmentVariable{.key = k, .value = v});
    }

    if (config.type == ExecutionType::InterpreterScript)
    {
        // 1. Resolve interpreter
        std::filesystem::path interp = config.interpreter_path;
        if (interp.empty())
        {
            interp = Project::ProjectDetector::resolve_python_interpreter(root);
        }
        resolved.process_spec.executable = interp.string();

        // 2. Resolve script
        std::filesystem::path script = config.target_file;
        if (script.is_relative())
        {
            script = root / script;
        }

        resolved.resolved_target = script;
        resolved.process_spec.arguments.push_back(script.string());
        for (const auto& arg : config.arguments)
        {
            resolved.process_spec.arguments.push_back(arg);
        }

        if (!std::filesystem::exists(script, ec))
        {
            resolved.error_message = "Script file not found: " + script.string();
        }

        return resolved;
    }

    // BinaryExecutable
    std::filesystem::path bin_target = config.target_file;
    std::string bin_name = bin_target.string();

#if defined(_WIN32)
    const std::string ext = ".exe";
#else
    const std::string ext = "";
#endif

    // If target doesn't already have extension, check candidates
    std::vector<std::filesystem::path> candidates;
    if (bin_target.is_absolute() && std::filesystem::exists(bin_target, ec))
    {
        candidates.push_back(bin_target);
    }
    else
    {
        std::string filename_with_ext = bin_name;
        if (bin_target.extension().empty())
        {
            filename_with_ext += ext;
        }

        // Check preset output dir
        if (!preset.empty())
        {
            candidates.push_back(root / "build" / preset / "bin" / config.build_config / filename_with_ext);
            candidates.push_back(root / "build" / preset / "bin" / filename_with_ext);
            candidates.push_back(root / "build" / preset / filename_with_ext);
        }

        // Generic build dirs
        candidates.push_back(root / "build" / "bin" / config.build_config / filename_with_ext);
        candidates.push_back(root / "build" / "bin" / filename_with_ext);
        candidates.push_back(root / "build" / filename_with_ext);

        // Cargo target dirs
        const std::string cargo_profile = (config.build_config == "Release" || config.build_config == "release") ? "release" : "debug";
        candidates.push_back(root / "target" / cargo_profile / filename_with_ext);

        // Root dir (e.g. FreePascal)
        candidates.push_back(root / filename_with_ext);
    }

    std::filesystem::path found_path;
    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate, ec))
        {
            found_path = candidate;
            break;
        }
    }

    if (!found_path.empty())
    {
        resolved.resolved_target = found_path;
        resolved.process_spec.executable = found_path.string();
        resolved.needs_build = false;
    }
    else
    {
        // Not yet built
        resolved.needs_build = true;
        if (!candidates.empty())
        {
            resolved.resolved_target = candidates.front();
            resolved.process_spec.executable = candidates.front().string();
        }
        else
        {
            resolved.resolved_target = bin_target;
            resolved.process_spec.executable = bin_target.string();
        }
    }

    for (const auto& arg : config.arguments)
    {
        resolved.process_spec.arguments.push_back(arg);
    }

    return resolved;
}

} // namespace Zenvra::Services::Run
