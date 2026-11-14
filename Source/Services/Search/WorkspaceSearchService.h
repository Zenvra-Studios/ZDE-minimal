#pragma once

#include <filesystem>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Search
{

struct TextMatch
{
    uint32_t line_number{0};
    uint32_t character_start{0};
    uint32_t length{0};
    std::string line_content;
};

struct FileTextMatch
{
    std::filesystem::path file_path;
    std::string relative_path;
    std::vector<TextMatch> matches;
};

struct SearchOptions
{
    bool match_case{false};
    bool match_whole_word{false};
    bool use_regex{false};
    std::vector<std::string> include_patterns;
    std::vector<std::string> exclude_patterns;
    std::size_t max_results{2000};
};

using SearchProgressCallback = std::function<void(const FileTextMatch&)>;

class WorkspaceSearchService
{
public:
    WorkspaceSearchService() = default;

    [[nodiscard]] static std::vector<FileTextMatch> search(
        const std::filesystem::path& root_directory,
        std::string_view query,
        const SearchOptions& options = {},
        SearchProgressCallback on_match = nullptr);

    [[nodiscard]] static std::future<std::vector<FileTextMatch>> search_async(
        const std::filesystem::path& root_directory,
        std::string query,
        SearchOptions options = {},
        SearchProgressCallback on_match = nullptr);
};

} // namespace Zenvra::Services::Search

