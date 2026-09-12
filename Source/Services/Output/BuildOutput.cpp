#include "Services/Output/BuildOutput.h"

#include <regex>
#include <sstream>

namespace Zenvra::Services::Output
{

std::vector<BuildIssue> BuildOutputParser::parse(std::string_view text)
{
    std::vector<BuildIssue> issues;
    std::string str(text);
    std::istringstream stream(str);
    std::string line;

    // Pattern 1: GCC / Clang / Rust: path:line:col: (error|warning|note): msg
    // or path:line: (error|warning|note): msg
    const std::regex gcc_pattern(R"(^([^:\n]+):(\d+)(?::(\d+))?:\s*(error|warning|note|info):\s*(.*)$)", std::regex::icase);

    // Pattern 2: MSVC: path(line,col): error C...: msg or path(line): error ...
    const std::regex msvc_pattern(R"(^([^(]+)\((\d+)(?:,(\d+))?\)\s*:\s*(error|warning|note|info)\s*([A-Za-z0-9]+)?:\s*(.*)$)", std::regex::icase);

    // Pattern 3: Python traceback:   File "path", line 123, in ...
    const std::regex py_pattern(R"re(^\s*File "([^"]+)", line (\d+)(?:, in (.*))?)re");

    while (std::getline(stream, line))
    {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        std::smatch m;
        if (std::regex_search(line, m, gcc_pattern))
        {
            BuildIssue issue;
            issue.file_path = m[1].str();
            issue.line = std::stoi(m[2].str());
            if (m[3].matched)
            {
                issue.column = std::stoi(m[3].str());
            }
            std::string sev_str = m[4].str();
            for (char& c : sev_str) c = static_cast<char>(::tolower(c));
            if (sev_str == "error") issue.severity = IssueSeverity::Error;
            else if (sev_str == "warning") issue.severity = IssueSeverity::Warning;
            else issue.severity = IssueSeverity::Info;

            issue.message = m[5].str();
            issue.raw_text = line;
            issues.push_back(std::move(issue));
        }
        else if (std::regex_search(line, m, msvc_pattern))
        {
            BuildIssue issue;
            issue.file_path = m[1].str();
            issue.line = std::stoi(m[2].str());
            if (m[3].matched)
            {
                issue.column = std::stoi(m[3].str());
            }
            std::string sev_str = m[4].str();
            for (char& c : sev_str) c = static_cast<char>(::tolower(c));
            if (sev_str == "error") issue.severity = IssueSeverity::Error;
            else if (sev_str == "warning") issue.severity = IssueSeverity::Warning;
            else issue.severity = IssueSeverity::Info;

            issue.message = m[6].str();
            issue.raw_text = line;
            issues.push_back(std::move(issue));
        }
        else if (std::regex_search(line, m, py_pattern))
        {
            BuildIssue issue;
            issue.file_path = m[1].str();
            issue.line = std::stoi(m[2].str());
            issue.severity = IssueSeverity::Error;
            issue.message = m[3].matched ? ("In " + m[3].str()) : "Exception trace";
            issue.raw_text = line;
            issues.push_back(std::move(issue));
        }
    }

    return issues;
}

} // namespace Zenvra::Services::Output
