#include "UI/Editor/EditorFileCrud.h"

#include <fstream>

namespace Zenvra::UI::Editor
{

std::optional<TextFileSnapshot> EditorFileCrud::read(
    const std::filesystem::path& path) const
{
    return EditorFileSystem::read_text_file(path);
}

std::optional<TextFileSnapshot> EditorFileCrud::create(
    const std::filesystem::path& path) const
{
    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(path, error);
    if (error || std::filesystem::exists(absolute_path, error) ||
        !std::filesystem::is_directory(absolute_path.parent_path(), error))
    {
        return std::nullopt;
    }
    std::ofstream stream(absolute_path, std::ios::binary | std::ios::out);
    if (!stream)
    {
        return std::nullopt;
    }
    stream.close();
    return read(absolute_path);
}

bool EditorFileCrud::update(
    const std::filesystem::path& path,
    std::span<const std::string> lines,
    std::string_view line_ending) const
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    const std::string_view separator = line_ending == "CRLF" ? "\r\n" : "\n";
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        stream.write(lines[index].data(), static_cast<std::streamsize>(lines[index].size()));
        if (index + 1 < lines.size())
        {
            stream.write(separator.data(), static_cast<std::streamsize>(separator.size()));
        }
    }
    stream.flush();
    return stream.good();
}

bool EditorFileCrud::rename(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) const
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) ||
        std::filesystem::exists(destination, error) ||
        !std::filesystem::is_directory(destination.parent_path(), error))
    {
        return false;
    }
    std::filesystem::rename(source, destination, error);
    return !error;
}

bool EditorFileCrud::remove(const std::filesystem::path& path) const
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) &&
        std::filesystem::remove(path, error) && !error;
}

std::filesystem::path EditorFileCrud::next_available_path(
    const std::filesystem::path& directory,
    std::string_view extension) const
{
    std::string normalized_extension{extension};
    if (!normalized_extension.empty() && normalized_extension.front() != '.')
    {
        normalized_extension.insert(normalized_extension.begin(), '.');
    }
    if (normalized_extension.empty())
    {
        normalized_extension = ".txt";
    }

    std::error_code error;
    for (std::size_t suffix = 1; suffix < 10000; ++suffix)
    {
        const std::string name = suffix == 1
            ? "Untitled" + normalized_extension
            : "Untitled-" + std::to_string(suffix) + normalized_extension;
        const std::filesystem::path candidate = directory / name;
        if (!std::filesystem::exists(candidate, error))
        {
            return candidate;
        }
        error.clear();
    }
    return directory / ("Untitled-" + std::to_string(10000) + normalized_extension);
}

} // namespace Zenvra::UI::Editor
