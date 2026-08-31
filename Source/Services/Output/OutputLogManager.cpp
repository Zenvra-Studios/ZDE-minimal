#include "Services/Output/OutputLogManager.h"

#include <sstream>

namespace Zenvra::Services::Output
{

OutputLogManager::OutputLogManager()
{
    // Seed initial informational banners
    m_logs[OutputCategory::Build].push_back("[Build] Ready. Click 'Build' or press Ctrl+B / Cmd+B to compile.");
    m_logs[OutputCategory::CMake].push_back("[CMake] Ready.");
    m_logs[OutputCategory::Runner].push_back("[Runner] Ready.");
    m_logs[OutputCategory::LanguageServer].push_back("[Language Server] Ready.");
    m_logs[OutputCategory::General].push_back("[General] ZDE Studio System Output Ready.");
}

OutputLogManager& OutputLogManager::instance()
{
    static auto* s_instance = new OutputLogManager();
    return *s_instance;
}

void OutputLogManager::append_line(OutputCategory category, std::string_view line)
{
    std::vector<LogListener> listeners_copy;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto& queue = m_logs[category];
        if (queue.size() >= max_lines_per_category)
        {
            queue.pop_front();
        }
        queue.emplace_back(line);
        listeners_copy = m_listeners;
    }

    for (const auto& listener : listeners_copy)
    {
        if (listener)
        {
            listener(category, line);
        }
    }
}

void OutputLogManager::append_text(OutputCategory category, std::string_view text)
{
    std::string line;
    std::istringstream stream{std::string(text)};
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        append_line(category, line);
    }
}

void OutputLogManager::clear(OutputCategory category)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logs[category].clear();
}

void OutputLogManager::clear_all()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logs.clear();
}

std::vector<std::string> OutputLogManager::get_lines(OutputCategory category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_logs.find(category);
    if (it == m_logs.end())
    {
        return {};
    }
    return {it->second.begin(), it->second.end()};
}

std::size_t OutputLogManager::get_line_count(OutputCategory category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_logs.find(category);
    return (it != m_logs.end()) ? it->second.size() : 0;
}

std::string OutputLogManager::get_combined_text(OutputCategory category) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_logs.find(category);
    if (it == m_logs.end())
    {
        return {};
    }
    std::ostringstream oss;
    for (const auto& line : it->second)
    {
        oss << line << '\n';
    }
    return oss.str();
}

void OutputLogManager::add_listener(LogListener listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.push_back(std::move(listener));
}

} // namespace Zenvra::Services::Output
