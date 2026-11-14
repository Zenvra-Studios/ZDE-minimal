#include "CustomProvider.h"
#include "Source/Services/LSP/Process/ProcessSupervisor.h"

namespace Zenvra::Services::Installation::Providers
{

bool CustomProvider::is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const
{
    return !install_spec.executable.empty();
}

std::future<InstallResult> CustomProvider::install_async(
    const Catalog::CatalogInstallation& install_spec,
    const std::filesystem::path& target_directory,
    InstallProgressCallback progress_callback)
{
    return std::async(std::launch::async, [install_spec, target_directory, progress_callback]() {
        InstallResult result;

        if (progress_callback)
            progress_callback(10.0F, "Running custom installer...");

        LSP::Process::ProcessSupervisor supervisor;
        LSP::Process::ProcessSpec spec;

#if defined(_WIN32)
        spec.executable = "cmd.exe";
        spec.arguments = {"/c", install_spec.executable};
#else
        spec.executable = "/bin/sh";
        spec.arguments = {"-c", install_spec.executable};
#endif
        spec.working_directory = target_directory;

        if (!supervisor.start(spec))
        {
            result.success = false;
            result.error_message = "Failed to launch custom install command";
            return result;
        }

        int exit_code = supervisor.wait_for_exit();
        if (exit_code != 0)
        {
            result.success = false;
            result.error_message = "Custom install command failed with exit code " + std::to_string(exit_code);
            return result;
        }

        result.success = true;
        result.installed_executable = target_directory / install_spec.executable;

        if (progress_callback)
            progress_callback(100.0F, "Custom installation finished");

        return result;
    });
}

} // namespace Zenvra::Services::Installation::Providers

