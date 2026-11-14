#include "DirectDownloadProvider.h"
#include "Source/Services/Installation/Download/DownloadManager.h"
#include "Source/Services/Installation/Extraction/ArchiveExtractor.h"
#include "Source/Services/Installation/Extraction/SafeExtract.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Zenvra::Services::Installation::Providers
{

bool DirectDownloadProvider::is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const
{
    return !install_spec.download_url.empty();
}

std::future<InstallResult> DirectDownloadProvider::install_async(
    const Catalog::CatalogInstallation& install_spec,
    const std::filesystem::path& target_directory,
    InstallProgressCallback progress_callback)
{
    return std::async(std::launch::async, [install_spec, target_directory, progress_callback]() {
        InstallResult result;

        if (progress_callback)
            progress_callback(10.0F, "Downloading direct asset...");

        std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "zenvra_install" / "direct";
        std::filesystem::create_directories(temp_dir);

        std::filesystem::path temp_file = temp_dir / "downloaded_file";
        Download::DownloadManager downloader;

        bool downloaded = downloader.download_sync(
            install_spec.download_url,
            temp_file,
            install_spec.sha256,
            [progress_callback](const Download::DownloadProgress& p) {
                if (progress_callback)
                    progress_callback(10.0F + p.percentage * 0.5F, "Downloading...");
            });

        if (!downloaded)
        {
            result.success = false;
            result.error_message = "Direct download failed from " + install_spec.download_url;
            return result;
        }

        if (progress_callback)
            progress_callback(70.0F, "Deploying package...");

        std::filesystem::create_directories(target_directory);
        bool extracted = false;
        if (Extraction::ArchiveExtractor::is_archive(temp_file))
        {
            extracted = Extraction::ArchiveExtractor::extract(temp_file, target_directory);
        }
        else
        {
            std::filesystem::path target_exe = target_directory / install_spec.executable;
#ifdef _WIN32
            extracted = CopyFileW(temp_file.c_str(), target_exe.c_str(), FALSE) != FALSE;
#else
            std::error_code ec;
            std::filesystem::copy_file(temp_file, target_exe, std::filesystem::copy_options::overwrite_existing, ec);
            extracted = !ec;
#endif
        }

        std::error_code ec;
        std::filesystem::remove(temp_file, ec);

        if (!extracted)
        {
            result.success = false;
            result.error_message = "Deployment failed for direct download package";
            return result;
        }

        result.success = true;
        result.installed_executable = target_directory / install_spec.executable;

        if (progress_callback)
            progress_callback(100.0F, "Installed successfully");

        return result;
    });
}

} // namespace Zenvra::Services::Installation::Providers

