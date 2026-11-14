#include "FileSearchService.h"

#include <algorithm>

namespace Zenvra::Services::Search
{

int FileSearchService::fuzzy_match_score(std::string_view text, std::string_view pattern)
{
    if (pattern.empty())
        return 100;
    if (text.empty() || pattern.size() > text.size())
        return -1;

    std::size_t pattern_idx = 0;
    int score = 0;
    int consecutive = 0;

    for (std::size_t i = 0; i < text.size() && pattern_idx < pattern.size(); ++i)
    {
        char t = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        char p = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[pattern_idx])));

        if (t == p)
        {
            ++pattern_idx;
            score += 10 + (consecutive * 5);
            consecutive++;

            // Bonus for matching start of segment
            if (i == 0 || text[i - 1] == '/' || text[i - 1] == '\\' || text[i - 1] == '_' || text[i - 1] == '-')
                score += 20;
        }
        else
        {
            consecutive = 0;
        }
    }

    return (pattern_idx == pattern.size()) ? score : -1;
}

std::vector<FileSearchResult> FileSearchService::search_files(
    const std::filesystem::path& root_directory,
    std::string_view query,
    std::size_t max_results)
{
    std::vector<FileSearchResult> results;
    std::error_code ec;

    if (!std::filesystem::exists(root_directory, ec))
        return results;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_directory, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;

        std::string rel = std::filesystem::relative(entry.path(), root_directory, ec).string();
        if (rel.find(".git") != std::string::npos ||
            rel.find("build") != std::string::npos ||
            rel.find("target") != std::string::npos ||
            rel.find("node_modules") != std::string::npos)
        {
            continue;
        }

        int score = fuzzy_match_score(rel, query);
        if (score >= 0)
        {
            results.push_back({entry.path(), std::move(rel), score});
        }
    }

    std::sort(results.begin(), results.end(), [](const FileSearchResult& a, const FileSearchResult& b) {
        return a.score > b.score;
    });

    if (results.size() > max_results)
        results.resize(max_results);

    return results;
}

} // namespace Zenvra::Services::Search

