#include "SystemPackageProvider.h"
#include "Source/Services/LSP/Process/ProcessSupervisor.h"
#include "Source/Services/LSP/Discovery/ServerDiscovery.h"

namespace Zenvra::Services::Installation::Providers
{

bool SystemPackageProvider::is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const
{
    if (install_spec.package_manager.empty())
        return false;

    return LSP::Discovery::ServerDiscovery::is_command_available(install_spec.package_manager);
}

std::future<InstallResult> SystemPackageProvider::install_async(
    const Catalog::CatalogInstallation& install_spec,
    const std::filesystem::path& /*target_directory*/,
    InstallProgressCallback progress_callback)
{
    return std::async(std::launch::async, [install_spec, progress_callback]() {
        InstallResult result;

        if (progress_callback)
            progress_callback(10.0F, "Executing system package manager: " + install_spec.package_manager);

        LSP::Process::ProcessSupervisor supervisor;
        LSP::Process::ProcessSpec spec;
        spec.executable = install_spec.package_manager;

        if (install_spec.package_manager == "winget")
        {
            spec.arguments = {"install", "--id", install_spec.package_name, "-e", "--accept-source-agreements", "--accept-package-agreements"};
        }
        else if (install_spec.package_manager == "scoop")
        {
            spec.arguments = {"install", install_spec.package_name};
        }
        else if (install_spec.package_manager == "choco")
        {
            spec.arguments = {"install", install_spec.package_name, "-y"};
        }
        else if (install_spec.package_manager == "brew")
        {
            spec.arguments = {"install", install_spec.package_name};
        }
        else if (install_spec.package_manager == "apt" || install_spec.package_manager == "apt-get")
        {
            spec.arguments = {"install", "-y", install_spec.package_name};
        }
        else
        {
            spec.arguments = {"install", install_spec.package_name};
        }

        if (!supervisor.start(spec))
        {
            result.success = false;
            result.error_message = "Failed to launch package manager " + install_spec.package_manager;
            return result;
        }

        int exit_code = supervisor.wait_for_exit();
        if (exit_code != 0)
        {
            result.success = false;
            result.error_message = "Package manager exited with error code " + std::to_string(exit_code);
            return result;
        }

        auto found_path = LSP::Discovery::ServerDiscovery::find_in_system_path(install_spec.executable);
        if (found_path)
        {
            result.success = true;
            result.installed_executable = *found_path;
        }
        else
        {
            result.success = true;
            result.installed_executable = install_spec.executable;
        }

        if (progress_callback)
            progress_callback(100.0F, "System package installed successfully");

        return result;
    });
}

} // namespace Zenvra::Services::Installation::Providers

