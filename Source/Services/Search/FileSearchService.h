#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Search
{

struct FileSearchResult
{
    std::filesystem::path path;
    std::string relative_path;
    int score{0};
};

class FileSearchService
{
public:
    FileSearchService() = default;

    [[nodiscard]] static std::vector<FileSearchResult> search_files(
        const std::filesystem::path& root_directory,
        std::string_view query,
        std::size_t max_results = 100);

    [[nodiscard]] static int fuzzy_match_score(std::string_view text, std::string_view pattern);
};

} // namespace Zenvra::Services::Search

