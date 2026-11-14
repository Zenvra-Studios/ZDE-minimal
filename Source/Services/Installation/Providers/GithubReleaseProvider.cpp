#include "GithubReleaseProvider.h"
#include "Source/Services/Installation/Download/DownloadManager.h"
#include "Source/Services/Installation/Extraction/ArchiveExtractor.h"
#include "Source/Services/Installation/Extraction/SafeExtract.h"
#include "Source/Services/Installation/Verification/Sha256Verifier.h"

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

bool GithubReleaseProvider::is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const
{
    return !install_spec.download_url.empty();
}

std::future<InstallResult> GithubReleaseProvider::install_async(
    const Catalog::CatalogInstallation& install_spec,
    const std::filesystem::path& target_directory,
    InstallProgressCallback progress_callback)
{
    return std::async(std::launch::async, [install_spec, target_directory, progress_callback]() {
        InstallResult result;

        if (progress_callback)
            progress_callback(10.0F, "Downloading GitHub release asset...");

        std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "zenvra_install" / install_spec.provider;
        std::filesystem::create_directories(temp_dir);

        std::filesystem::path temp_archive = temp_dir / "downloaded_archive";
        Download::DownloadManager downloader;

        bool downloaded = downloader.download_sync(
            install_spec.download_url,
            temp_archive,
            install_spec.sha256,
            [progress_callback](const Download::DownloadProgress& p) {
                if (progress_callback)
                    progress_callback(10.0F + p.percentage * 0.4F, "Downloading...");
            });

        if (!downloaded)
        {
            result.success = false;
            result.error_message = "Failed to download release archive from " + install_spec.download_url;
            return result;
        }

        if (progress_callback)
            progress_callback(60.0F, "Extracting package...");

        std::filesystem::create_directories(target_directory);
        bool extracted = false;
        if (Extraction::ArchiveExtractor::is_archive(temp_archive))
        {
            extracted = Extraction::ArchiveExtractor::extract(temp_archive, target_directory);
        }
        else
        {
            // Direct executable
            std::filesystem::path bin_target = target_directory / install_spec.executable;
#ifdef _WIN32
            extracted = CopyFileW(temp_archive.c_str(), bin_target.c_str(), FALSE) != FALSE;
#else
            std::error_code ec;
            std::filesystem::copy_file(temp_archive, bin_target, std::filesystem::copy_options::overwrite_existing, ec);
            extracted = !ec;
#endif
        }

        std::error_code ec;
        std::filesystem::remove(temp_archive, ec);

        if (!extracted)
        {
            result.success = false;
            result.error_message = "Failed to extract package to " + target_directory.string();
            return result;
        }

        std::filesystem::path final_exe = target_directory / install_spec.executable;
        result.success = true;
        result.installed_executable = final_exe;

        if (progress_callback)
            progress_callback(100.0F, "Installation complete");

        return result;
    });
}

} // namespace Zenvra::Services::Installation::Providers

