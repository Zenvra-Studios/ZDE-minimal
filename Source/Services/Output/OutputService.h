#pragma once

#include "Services/Output/BuildOutput.h"
#include "Services/Output/OutputLogManager.h"

#include <functional>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Output
{

class OutputService
{
public:
    OutputService() = default;

    void log_build(std::string_view message);
    void log_run(std::string_view message);
    void log_cmake(std::string_view message);
    void log_general(std::string_view message);

    void append_text(OutputCategory category, std::string_view text);
    void append_line(OutputCategory category, std::string_view line);
    void clear(OutputCategory category);
    void clear_all();

    [[nodiscard]] std::vector<std::string> get_lines(OutputCategory category) const;
    [[nodiscard]] std::string get_combined_text(OutputCategory category) const;
    [[nodiscard]] std::vector<BuildIssue> parse_diagnostics(OutputCategory category) const;

    void add_listener(std::function<void(OutputCategory, std::string_view)> listener);
};

} // namespace Zenvra::Services::Output
