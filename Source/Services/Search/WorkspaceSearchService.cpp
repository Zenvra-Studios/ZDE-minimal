#include "WorkspaceSearchService.h"

#include <fstream>
#include <regex>
#include <algorithm>

namespace Zenvra::Services::Search
{

namespace
{
bool should_skip_directory(const std::filesystem::path& path)
{
    std::string s = path.string();
    return s.find(".git") != std::string::npos ||
           s.find("build") != std::string::npos ||
           s.find("target") != std::string::npos ||
           s.find("node_modules") != std::string::npos ||
           s.find(".vs") != std::string::npos;
}

bool case_insensitive_find(std::string_view haystack, std::string_view needle, std::size_t& pos)
{
    auto it = std::search(
        haystack.begin() + pos, haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });

    if (it != haystack.end())
    {
        pos = std::distance(haystack.begin(), it);
        return true;
    }
    return false;
}
} // namespace

std::vector<FileTextMatch> WorkspaceSearchService::search(
    const std::filesystem::path& root_directory,
    std::string_view query,
    const SearchOptions& options,
    SearchProgressCallback on_match)
{
    std::vector<FileTextMatch> all_matches;
    if (query.empty())
        return all_matches;

    std::error_code ec;
    if (!std::filesystem::exists(root_directory, ec))
        return all_matches;

    std::unique_ptr<std::regex> rx;
    if (options.use_regex)
    {
        try
        {
            auto flags = std::regex_constants::ECMAScript;
            if (!options.match_case)
                flags |= std::regex_constants::icase;
            rx = std::make_unique<std::regex>(std::string(query), flags);
        }
        catch (...)
        {
            return all_matches;
        }
    }

    std::size_t total_matches = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_directory, ec))
    {
        if (total_matches >= options.max_results)
            break;

        if (!entry.is_regular_file(ec))
            continue;

        if (should_skip_directory(entry.path()))
            continue;

        std::ifstream file(entry.path());
        if (!file.is_open())
            continue;

        FileTextMatch file_match;
        file_match.file_path = entry.path();
        file_match.relative_path = std::filesystem::relative(entry.path(), root_directory, ec).string();

        std::string line;
        uint32_t line_number = 1;

        while (std::getline(file, line) && total_matches < options.max_results)
        {
            if (options.use_regex && rx)
            {
                std::sregex_iterator words_begin(line.begin(), line.end(), *rx);
                std::sregex_iterator words_end;
                for (auto i = words_begin; i != words_end && total_matches < options.max_results; ++i)
                {
                    TextMatch tm;
                    tm.line_number = line_number;
                    tm.character_start = static_cast<uint32_t>(i->position());
                    tm.length = static_cast<uint32_t>(i->length());
                    tm.line_content = line;
                    file_match.matches.push_back(std::move(tm));
                    ++total_matches;
                }
            }
            else
            {
                std::size_t pos = 0;
                while (pos < line.size() && total_matches < options.max_results)
                {
                    bool found = false;
                    if (options.match_case)
                    {
                        pos = line.find(query, pos);
                        found = (pos != std::string::npos);
                    }
                    else
                    {
                        found = case_insensitive_find(line, query, pos);
                    }

                    if (!found)
                        break;

                    bool valid_word = true;
                    if (options.match_whole_word)
                    {
                        bool before_ok = (pos == 0) || !std::isalnum(static_cast<unsigned char>(line[pos - 1]));
                        bool after_ok = (pos + query.size() >= line.size()) || !std::isalnum(static_cast<unsigned char>(line[pos + query.size()]));
                        valid_word = before_ok && after_ok;
                    }

                    if (valid_word)
                    {
                        TextMatch tm;
                        tm.line_number = line_number;
                        tm.character_start = static_cast<uint32_t>(pos);
                        tm.length = static_cast<uint32_t>(query.size());
                        tm.line_content = line;
                        file_match.matches.push_back(std::move(tm));
                        ++total_matches;
                    }

                    pos += query.size();
                }
            }

            ++line_number;
        }

        if (!file_match.matches.empty())
        {
            if (on_match)
                on_match(file_match);
            all_matches.push_back(std::move(file_match));
        }
    }

    return all_matches;
}

std::future<std::vector<FileTextMatch>> WorkspaceSearchService::search_async(
    const std::filesystem::path& root_directory,
    std::string query,
    SearchOptions options,
    SearchProgressCallback on_match)
{
    return std::async(std::launch::async, [root_directory, q = std::move(query), opts = std::move(options), cb = std::move(on_match)]() {
        return search(root_directory, q, opts, cb);
    });
}

} // namespace Zenvra::Services::Search

