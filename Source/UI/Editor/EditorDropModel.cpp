#include "UI/Editor/EditorDropModel.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace Zenvra::UI::Editor
{

std::vector<std::filesystem::path> EditorDropModel::collect_files(
    std::span<const std::filesystem::path> dropped_paths)
{
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> known_paths;
    const auto append_file = [&result, &known_paths](const std::filesystem::path& path) {
        if (result.size() >= maximum_dropped_files)
        {
            return;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
        {
            return;
        }
        std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            error.clear();
            resolved = std::filesystem::absolute(path, error);
        }
        if (!error && known_paths.insert(resolved.string()).second)
        {
            result.push_back(std::move(resolved));
        }
    };

    for (const std::filesystem::path& dropped_path : dropped_paths)
    {
        if (result.size() >= maximum_dropped_files)
        {
            break;
        }
        std::error_code error;
        if (std::filesystem::is_regular_file(dropped_path, error))
        {
            append_file(dropped_path);
            continue;
        }
        error.clear();
        if (!std::filesystem::is_directory(dropped_path, error))
        {
            continue;
        }

        std::vector<std::filesystem::path> directory_files;
        std::filesystem::recursive_directory_iterator iterator(
            dropped_path,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end &&
               directory_files.size() < maximum_dropped_files)
        {
            if (iterator->is_regular_file(error) && !error)
            {
                directory_files.push_back(iterator->path());
            }
            error.clear();
            iterator.increment(error);
        }
        std::sort(directory_files.begin(), directory_files.end());
        for (const std::filesystem::path& file : directory_files)
        {
            append_file(file);
            if (result.size() >= maximum_dropped_files)
            {
                break;
            }
        }
    }
    return result;
}

} // namespace Zenvra::UI::Editor
