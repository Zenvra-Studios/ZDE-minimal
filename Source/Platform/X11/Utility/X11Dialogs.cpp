#include "Platform/PlatformDialogs.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace Zenvra::Platform
{

/// Runs a shell command and returns its stdout output (trailing newline stripped).
/// Returns an empty string on failure or if the process returns a non-zero exit code.
static std::string run_dialog_command(const char* command)
{
    // NOLINTNEXTLINE(cert-env33-c)
    FILE* pipe = ::popen(command, "r");
    if (pipe == nullptr)
    {
        return {};
    }

    std::string result;
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        result += buffer.data();
    }
    const int exit_code = ::pclose(pipe);
    if (exit_code != 0)
    {
        return {};
    }

    // Strip trailing whitespace / newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
    {
        result.pop_back();
    }
    return result;
}

/// Detect whether a command is available on PATH.
static bool command_exists(const char* name)
{
    std::string check = "command -v ";
    check += name;
    check += " >/dev/null 2>&1";
    // NOLINTNEXTLINE(cert-env33-c)
    return std::system(check.c_str()) == 0;
}

std::optional<std::filesystem::path> open_folder_dialog()
{
    if (command_exists("kdialog"))
    {
        const std::string selected = run_dialog_command(
            "kdialog --title \"Open Folder\" --getexistingdirectory \"$HOME\" 2>/dev/null");
        if (selected.empty())
        {
            return std::nullopt;
        }
        return std::filesystem::path{selected};
    }

    if (command_exists("zenity"))
    {
        const std::string selected = run_dialog_command(
            "zenity --file-selection --directory --title=\"Open Folder\" 2>/dev/null");
        if (selected.empty())
        {
            return std::nullopt;
        }
        return std::filesystem::path{selected};
    }

    if (command_exists("yad"))
    {
        const std::string selected = run_dialog_command(
            "yad --file-selection --directory --title=\"Open Folder\" 2>/dev/null");
        if (selected.empty())
        {
            return std::nullopt;
        }
        return std::filesystem::path{selected};
    }

    return std::nullopt;
}

bool folder_dialog_available()
{
    return command_exists("kdialog") || command_exists("zenity") ||
        command_exists("yad");
}

std::optional<std::string> input_dialog(
    const std::string& title, const std::string& label,
    const std::string& default_value)
{
    if (command_exists("kdialog"))
    {
        std::string cmd = "kdialog --title \"" + title + "\" --inputbox \"" + label + "\"";
        if (!default_value.empty())
        {
            cmd += " \"" + default_value + "\"";
        }
        else
        {
            cmd += " \"\"";
        }
        cmd += " 2>/dev/null";
        const std::string result = run_dialog_command(cmd.c_str());
        if (result.empty())
        {
            return std::nullopt;
        }
        return result;
    }

    if (command_exists("zenity"))
    {
        std::string cmd = "zenity --entry --title=\"" + title +
                          "\" --text=\"" + label + "\"";
        if (!default_value.empty())
        {
            cmd += " --entry-text=\"" + default_value + "\"";
        }
        cmd += " 2>/dev/null";
        const std::string result = run_dialog_command(cmd.c_str());
        if (result.empty())
        {
            return std::nullopt;
        }
        return result;
    }

    if (command_exists("yad"))
    {
        std::string cmd = "yad --entry --title=\"" + title +
                          "\" --text=\"" + label + "\"";
        if (!default_value.empty())
        {
            cmd += " --entry-text=\"" + default_value + "\"";
        }
        cmd += " 2>/dev/null";
        const std::string result = run_dialog_command(cmd.c_str());
        if (result.empty())
        {
            return std::nullopt;
        }
        return result;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> clone_repository_dialog()
{
    return open_folder_dialog();
}

} // namespace Zenvra::Platform
