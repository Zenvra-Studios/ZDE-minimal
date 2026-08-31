#include "UI/Editor/EditorFileSystem.h"

#include "Utility/TextEncoding.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string_view>

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
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" ||
           ext == ".avi" || ext == ".flv" || ext == ".wmv" || ext == ".ts" ||
           ext == ".m4v" || ext == ".ogv" || ext == ".3gp" || ext == ".vob" ||
           ext == ".rmvb" || ext == ".mjpg";
}

bool is_audio_file(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" ||
           ext == ".aac" || ext == ".m4a" || ext == ".opus" || ext == ".aiff" ||
           ext == ".wma" || ext == ".ac3" || ext == ".mid" || ext == ".midi";
}

bool is_image_file(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" ||
           ext == ".webp" || ext == ".bmp" || ext == ".ico" || ext == ".tiff" ||
           ext == ".svg" || ext == ".psd" || ext == ".tga";
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
