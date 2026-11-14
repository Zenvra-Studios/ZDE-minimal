#include "ArchiveExtractor.h"
#include "SafeExtract.h"
#include "Source/Services/LSP/Process/ProcessSupervisor.h"

#include <iostream>
#include <algorithm>

namespace Zenvra::Services::Installation::Extraction
{

bool ArchiveExtractor::is_archive(const std::filesystem::path& file_path)
{
    std::string ext = file_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".zip" || ext == ".tgz" || ext == ".tar" || ext == ".gz" || ext == ".xz" || ext == ".bz2")
        return true;

    std::string name = file_path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return name.ends_with(".tar.gz") || name.ends_with(".tar.xz") || name.ends_with(".tar.bz2");
}

bool ArchiveExtractor::extract(
    const std::filesystem::path& archive_path,
    const std::filesystem::path& destination_dir,
    ExtractProgressCallback progress_callback)
{
    if (!std::filesystem::exists(archive_path))
        return false;

    if (!SafeExtract::ensure_directory_exists(destination_dir))
        return false;

    std::string name = archive_path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    LSP::Process::ProcessSupervisor supervisor;
    LSP::Process::ProcessSpec spec;

#if defined(_WIN32)
    if (name.ends_with(".zip"))
    {
        // Use tar (built into Windows 10/11) or PowerShell Expand-Archive
        spec.executable = "tar.exe";
        spec.arguments = {"-xf", archive_path.string(), "-C", destination_dir.string()};
        if (!supervisor.start(spec))
        {
            // Fallback to powershell
            spec.executable = "powershell.exe";
            spec.arguments = {"-NoProfile", "-NonInteractive", "-Command",
                              "Expand-Archive -Path '" + archive_path.string() +
                              "' -DestinationPath '" + destination_dir.string() + "' -Force"};
            if (!supervisor.start(spec))
                return false;
        }
    }
    else
    {
        // tar.gz, tar.xz, tar
        spec.executable = "tar.exe";
        spec.arguments = {"-xf", archive_path.string(), "-C", destination_dir.string()};
        if (!supervisor.start(spec))
            return false;
    }
#else
    // POSIX tar
    spec.executable = "tar";
    spec.arguments = {"-xf", archive_path.string(), "-C", destination_dir.string()};
    if (!supervisor.start(spec))
        return false;
#endif

    int exit_code = supervisor.wait_for_exit();
    if (exit_code != 0)
        return false;

    if (progress_callback)
    {
        ExtractProgress prog;
        prog.percentage = 100.0F;
        prog.current_entry = "Complete";
        progress_callback(prog);
    }

    return true;
}

std::vector<std::filesystem::path> ArchiveExtractor::list_entries(
    const std::filesystem::path& archive_path)
{
    std::vector<std::filesystem::path> entries;
    if (!std::filesystem::exists(archive_path))
        return entries;

    LSP::Process::ProcessSupervisor supervisor;
    LSP::Process::ProcessSpec spec;

#if defined(_WIN32)
    spec.executable = "tar.exe";
#else
    spec.executable = "tar";
#endif
    spec.arguments = {"-tf", archive_path.string()};

    std::string output;
    supervisor.set_stdout_handler([&output](std::string_view chunk) {
        output.append(chunk);
    });

    if (supervisor.start(spec))
    {
        supervisor.wait_for_exit();
        std::size_t start = 0;
        while (start < output.size())
        {
            std::size_t end = output.find_first_of("\r\n", start);
            if (end == std::string::npos)
                end = output.size();

            std::string line = output.substr(start, end - start);
            if (!line.empty())
                entries.emplace_back(line);

            start = output.find_first_not_of("\r\n", end);
        }
    }

    return entries;
}

} // namespace Zenvra::Services::Installation::Extraction

