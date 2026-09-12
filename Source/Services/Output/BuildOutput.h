#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Output
{

enum class IssueSeverity
{
    Info,
    Warning,
    Error
};

struct BuildIssue
{
    IssueSeverity severity = IssueSeverity::Info;
    std::filesystem::path file_path;
    int line = 0;
    int column = 0;
    std::string message;
    std::string raw_text;
};

class BuildOutputParser
{
public:
    [[nodiscard]] static std::vector<BuildIssue> parse(std::string_view text);
};

} // namespace Zenvra::Services::Output
