#include "UI/Editor/EditorFileSystem.h"
#include "UI/Editor/MediaPlayerView.h"

#include "Utility/TextEncoding.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <pwd.h>
#include <unistd.h>
#endif

namespace Zenvra::UI::Editor
{

namespace
{

constexpr std::uintmax_t maximum_editor_file_size = 8U * 1024U * 1024U;

std::string format_file_size(std::uintmax_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return std::string(buf);
    }
    if (bytes >= 1024ULL * 1024ULL)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return std::string(buf);
    }
    if (bytes >= 1024ULL)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        return std::string(buf);
    }
    return std::to_string(bytes) + " bytes";
}

bool is_video_file(const std::filesystem::path& path)
{
    return MediaPlayerView::is_video_file(path);
}

bool is_audio_file(const std::filesystem::path& path)
{
    return MediaPlayerView::is_audio_file(path);
}

bool is_image_file(const std::filesystem::path& path)
{
    return MediaPlayerView::is_image_file(path);
}

std::vector<std::string> build_decompiled_binary_preview(
    const std::filesystem::path& file_path,
    std::string_view contents,
    std::uintmax_t file_size)
{
    std::vector<std::string> lines;
    const std::string filename = file_path.filename().string();
    const std::string ext = file_path.extension().string();
    const std::string formatted_size = format_file_size(file_size);

    std::string kind = "Binary Stream";
    if (is_video_file(file_path)) kind = "Video Container (" + (ext.empty() ? "Video" : ext.substr(1)) + ")";
    else if (is_audio_file(file_path)) kind = "Audio Stream (" + (ext.empty() ? "Audio" : ext.substr(1)) + ")";
    else if (is_image_file(file_path)) kind = "Image Asset (" + (ext.empty() ? "Image" : ext.substr(1)) + ")";

    lines.emplace_back("// ============================================================================");
    lines.emplace_back("// ZDE Binary Decompiler & Hex Dump Inspector");
    lines.emplace_back("// File:     " + filename);
    lines.emplace_back("// Size:     " + formatted_size + " (" + std::to_string(file_size) + " bytes)");
    lines.emplace_back("// Type:     " + kind);
    if (is_video_file(file_path) || is_audio_file(file_path) || is_image_file(file_path))
    {
        lines.emplace_back("// Playback: Click the Media Output icon [>] next to Split button to toggle visual preview");
    }
    lines.emplace_back("// ============================================================================");
    lines.emplace_back("");
    lines.emplace_back("OFFSET    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  DECOMPILED ASCII");
    lines.emplace_back("--------  -----------------------  -----------------------  ----------------");

    constexpr std::size_t bytes_per_line = 16;
    constexpr std::size_t max_preview_bytes = 64 * 1024; // 64 KB preview
    const std::size_t preview_len = std::min(contents.size(), max_preview_bytes);

    for (std::size_t offset = 0; offset < preview_len; offset += bytes_per_line)
    {
        char offset_buf[16];
        std::snprintf(offset_buf, sizeof(offset_buf), "%08zX  ", offset);
        std::string line = offset_buf;

        const std::size_t chunk_size = std::min(bytes_per_line, preview_len - offset);

        for (std::size_t i = 0; i < bytes_per_line; ++i)
        {
            if (i == 8) line += " ";
            if (i < chunk_size)
            {
                char hex_buf[4];
                std::snprintf(hex_buf, sizeof(hex_buf), "%02X ", static_cast<unsigned char>(contents[offset + i]));
                line += hex_buf;
            }
            else
            {
                line += "   ";
            }
        }

        line += " |";
        for (std::size_t i = 0; i < chunk_size; ++i)
        {
            const unsigned char byte = static_cast<unsigned char>(contents[offset + i]);
            if (byte >= 32 && byte <= 126)
            {
                line += static_cast<char>(byte);
            }
            else
            {
                line += '.';
            }
        }
        line += "|";
        lines.push_back(std::move(line));
    }

    if (contents.size() > max_preview_bytes)
    {
        lines.emplace_back("");
        lines.emplace_back("[... Hex decompile preview truncated at 64 KB for performance ...]");
    }

    return lines;
}

std::vector<std::string> split_lines(std::string_view contents)
{
    std::vector<std::string> lines;
    std::size_t line_start = 0;
    while (line_start <= contents.size())
    {
        const std::size_t line_end = contents.find('\n', line_start);
        const std::size_t length = line_end == std::string_view::npos
            ? contents.size() - line_start
            : line_end - line_start;
        std::string line{contents.substr(line_start, length)};
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (line_end == std::string_view::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }
    if (lines.empty())
    {
        lines.emplace_back();
    }
    return lines;
}

bool is_app_or_system_dir(const std::filesystem::path& directory)
{
    std::error_code error;
    if (std::filesystem::exists(directory / "ZDE.exe", error) ||
        std::filesystem::exists(directory / "ZDE", error) ||
        std::filesystem::exists(directory / "unins000.exe", error) ||
        std::filesystem::exists(directory / "BootstrapperLib.dll", error) ||
        std::filesystem::exists(directory / "libBootstrapperLib.so", error) ||
        std::filesystem::exists(directory / "libBootstrapperLib.dylib", error) ||
        std::filesystem::exists(directory / "CMakeCache.txt", error) ||
        std::filesystem::is_directory(directory / "CMakeFiles", error))
    {
        return true;
    }
    const std::string path_str = directory.string();
    if (path_str.find("Program Files") != std::string::npos ||
        path_str.find("Windows\\System32") != std::string::npos ||
        path_str.find("/usr/bin") != std::string::npos ||
        path_str.find("/usr/lib") != std::string::npos)
    {
        return true;
    }
    return false;
}

bool looks_like_project_root(const std::filesystem::path& directory)
{
    if (is_app_or_system_dir(directory))
    {
        return false;
    }

    std::error_code error;
    if (std::filesystem::is_regular_file(directory / "CMakeLists.txt", error) ||
        std::filesystem::is_directory(directory / ".git", error) ||
        std::filesystem::is_directory(directory / ".zde", error) ||
        std::filesystem::is_regular_file(directory / "Cargo.toml", error) ||
        std::filesystem::is_regular_file(directory / "package.json", error) ||
        std::filesystem::is_regular_file(directory / "go.mod", error) ||
        std::filesystem::is_regular_file(directory / "Makefile", error) ||
        std::filesystem::is_regular_file(directory / "meson.build", error) ||
        std::filesystem::is_regular_file(directory / "BUILD.bazel", error))
    {
        return true;
    }
    if (std::filesystem::is_directory(directory / "Source", error) ||
        std::filesystem::is_directory(directory / "src", error))
    {
        return true;
    }
    return false;
}

} // namespace

std::filesystem::path EditorFileSystem::get_user_home_directory()
{
#if defined(_WIN32)
    std::array<wchar_t, 32768> user_profile{};
    const DWORD len = GetEnvironmentVariableW(
        L"USERPROFILE", user_profile.data(),
        static_cast<DWORD>(user_profile.size()));
    if (len > 0 && len < user_profile.size())
    {
        std::error_code ec;
        std::filesystem::path p{user_profile.data()};
        if (std::filesystem::is_directory(p, ec))
        {
            return p;
        }
    }

    // 2. Windows Shell Known Folder API (FOLDERID_Profile)
    PWSTR known_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &known_path)) && known_path != nullptr)
    {
        std::filesystem::path p{known_path};
        CoTaskMemFree(known_path);
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec))
        {
            return p;
        }
    }

    // 3. Check HOMEDRIVE + HOMEPATH
    std::array<wchar_t, 512> drive{};
    std::array<wchar_t, 32768> path{};
    const DWORD d_len = GetEnvironmentVariableW(
        L"HOMEDRIVE", drive.data(), static_cast<DWORD>(drive.size()));
    const DWORD p_len = GetEnvironmentVariableW(
        L"HOMEPATH", path.data(), static_cast<DWORD>(path.size()));
    if (d_len > 0 && p_len > 0)
    {
        std::wstring combined = std::wstring(drive.data()) + path.data();
        std::error_code ec;
        std::filesystem::path p{combined};
        if (std::filesystem::is_directory(p, ec))
        {
            return p;
        }
    }

    // 4. Resolve username dynamically via OS GetUserNameW API
    std::array<wchar_t, 512> username{};
    DWORD u_len = static_cast<DWORD>(username.size());
    if (GetUserNameW(username.data(), &u_len) && u_len > 0)
    {
        std::array<wchar_t, 64> sys_drive{};
        const DWORD sd_len = GetEnvironmentVariableW(
            L"SystemDrive", sys_drive.data(), static_cast<DWORD>(sys_drive.size()));
        const std::wstring drive_prefix = (sd_len > 0) ? sys_drive.data() : L"C:";
        std::filesystem::path candidate = std::filesystem::path(drive_prefix) / L"Users" / username.data();
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec))
        {
            return candidate;
        }
    }

    // 5. General fallback: SystemDrive\Users or root drive
    std::array<wchar_t, 64> sys_drive{};
    const DWORD sd_len = GetEnvironmentVariableW(
        L"SystemDrive", sys_drive.data(), static_cast<DWORD>(sys_drive.size()));
    const std::wstring drive_prefix = (sd_len > 0) ? sys_drive.data() : L"C:";
    std::filesystem::path users_dir = std::filesystem::path(drive_prefix) / L"Users";
    std::error_code ec;
    if (std::filesystem::is_directory(users_dir, ec))
    {
        return users_dir;
    }

    return std::filesystem::path{drive_prefix + L"\\"};
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        std::error_code ec;
        std::filesystem::path p{home};
        if (std::filesystem::is_directory(p, ec))
        {
            return p;
        }
    }

    struct passwd* pw = getpwuid(getuid());
    if (pw != nullptr && pw->pw_dir != nullptr && pw->pw_dir[0] != '\0')
    {
        std::error_code ec;
        std::filesystem::path p{pw->pw_dir};
        if (std::filesystem::is_directory(p, ec))
        {
            return p;
        }
    }

    return std::filesystem::path{"/"};
#endif
}

std::optional<std::filesystem::path> EditorFileSystem::find_project_root(
    const std::filesystem::path& start)
{
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(start, error);
    if (error)
    {
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(current, error))
    {
        current = current.parent_path();
    }
    while (!current.empty())
    {
        if (looks_like_project_root(current))
        {
            return std::filesystem::weakly_canonical(current, error);
        }
        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }
    return std::nullopt;
}

std::optional<TextFileSnapshot> EditorFileSystem::read_text_file(
    const std::filesystem::path& requested_path)
{
    std::error_code error;
    const std::filesystem::path current_directory = std::filesystem::current_path(error);
    if (error)
    {
        return std::nullopt;
    }
    const std::optional<std::filesystem::path> current_project_root =
        find_project_root(current_directory);
    std::filesystem::path resolved_path = requested_path;
    if (requested_path.is_relative())
    {
        const std::filesystem::path from_current = current_directory / requested_path;
        if (std::filesystem::is_regular_file(from_current, error))
        {
            resolved_path = from_current;
        }
        else if (current_project_root)
        {
            resolved_path = *current_project_root / requested_path;
        }
    }
    if (!std::filesystem::is_regular_file(resolved_path, error))
    {
        return std::nullopt;
    }
    const std::uintmax_t file_size = std::filesystem::file_size(resolved_path, error);
    if (error)
    {
        return std::nullopt;
    }

    std::ifstream stream(resolved_path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    const std::size_t read_size = static_cast<std::size_t>(std::min<std::uintmax_t>(
        file_size, maximum_editor_file_size));
    std::string contents(read_size, '\0');
    if (read_size > 0)
    {
        stream.read(contents.data(), static_cast<std::streamsize>(read_size));
        contents.resize(static_cast<std::size_t>(stream.gcount()));
    }

    TextFileSnapshot snapshot;
    snapshot.absolute_path = std::filesystem::weakly_canonical(resolved_path, error);
    if (error)
    {
        error.clear();
        snapshot.absolute_path = std::filesystem::absolute(resolved_path, error);
        if (error)
        {
            return std::nullopt;
        }
    }
    const std::optional<std::filesystem::path> source_project_root =
        find_project_root(snapshot.absolute_path);
    snapshot.project_root = source_project_root.value_or(snapshot.absolute_path.parent_path());
    snapshot.breadcrumbs = build_breadcrumbs(snapshot.absolute_path, snapshot.project_root);
    const bool truncated = file_size > contents.size();
    bool valid_text = contents.find('\0') == std::string::npos &&
        Utility::is_valid_utf8(contents);
    if (!valid_text && truncated)
    {
        for (std::size_t trim = 1; trim <= 3 && trim <= contents.size(); ++trim)
        {
            const std::string_view candidate =
                std::string_view{contents}.substr(0, contents.size() - trim);
            if (Utility::is_valid_utf8(candidate))
            {
                contents.resize(contents.size() - trim);
                valid_text = true;
                break;
            }
        }
    }
    if (!valid_text)
    {
        snapshot.lines = build_decompiled_binary_preview(snapshot.absolute_path, contents, file_size);
        snapshot.read_only = true;
        snapshot.binary_preview = true;
        snapshot.truncated = false;
        snapshot.line_ending = "LF";
        return snapshot;
    }
    if (contents.size() >= 3 &&
        static_cast<unsigned char>(contents[0]) == 0xEFU &&
        static_cast<unsigned char>(contents[1]) == 0xBBU &&
        static_cast<unsigned char>(contents[2]) == 0xBFU)
    {
        contents.erase(0, 3);
    }
    snapshot.lines = split_lines(contents);
    snapshot.truncated = truncated;
    snapshot.read_only = truncated;
    if (truncated)
    {
        snapshot.lines.emplace_back();
        snapshot.lines.emplace_back("[Text preview truncated - read only]");
    }
    snapshot.line_ending = contents.find("\r\n") != std::string::npos ? "CRLF" : "LF";
    return snapshot;
}

std::vector<BreadcrumbItem> EditorFileSystem::build_breadcrumbs(
    const std::filesystem::path& absolute_path,
    const std::filesystem::path& project_root)
{
    std::vector<BreadcrumbItem> breadcrumbs;
    if (!project_root.filename().empty())
    {
        breadcrumbs.push_back({project_root.filename().string(), BreadcrumbIconKind::Folder});
    }
    std::error_code error;
    const std::filesystem::path relative_path = std::filesystem::relative(
        absolute_path, project_root, error);
    const std::filesystem::path& display_path = error ? absolute_path : relative_path;
    std::vector<std::string> parts;
    for (const std::filesystem::path& part : display_path)
    {
        const std::string value = part.string();
        if (!value.empty() && value != ".")
        {
            parts.push_back(value);
        }
    }
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        const bool is_last = index + 1 == parts.size();
        breadcrumbs.push_back({
            parts[index],
            is_last ? BreadcrumbIconKind::File : BreadcrumbIconKind::Folder,
        });
    }
    return breadcrumbs;
}

} // namespace Zenvra::UI::Editor
