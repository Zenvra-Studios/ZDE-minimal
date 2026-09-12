#include "Services/Output/OutputService.h"

namespace Zenvra::Services::Output
{

void OutputService::log_build(std::string_view message)
{
    OutputLogManager::instance().append_line(OutputCategory::Build, message);
}

void OutputService::log_run(std::string_view message)
{
    OutputLogManager::instance().append_line(OutputCategory::Runner, message);
}

void OutputService::log_cmake(std::string_view message)
{
    OutputLogManager::instance().append_line(OutputCategory::CMake, message);
}

void OutputService::log_general(std::string_view message)
{
    OutputLogManager::instance().append_line(OutputCategory::General, message);
}

void OutputService::append_text(OutputCategory category, std::string_view text)
{
    OutputLogManager::instance().append_text(category, text);
}

void OutputService::append_line(OutputCategory category, std::string_view line)
{
    OutputLogManager::instance().append_line(category, line);
}

void OutputService::clear(OutputCategory category)
{
    OutputLogManager::instance().clear(category);
}

void OutputService::clear_all()
{
    OutputLogManager::instance().clear_all();
}

std::vector<std::string> OutputService::get_lines(OutputCategory category) const
{
    return OutputLogManager::instance().get_lines(category);
}

std::string OutputService::get_combined_text(OutputCategory category) const
{
    return OutputLogManager::instance().get_combined_text(category);
}

std::vector<BuildIssue> OutputService::parse_diagnostics(OutputCategory category) const
{
    const std::string text = get_combined_text(category);
    return BuildOutputParser::parse(text);
}

void OutputService::add_listener(std::function<void(OutputCategory, std::string_view)> listener)
{
    OutputLogManager::instance().add_listener(std::move(listener));
}

} // namespace Zenvra::Services::Output
