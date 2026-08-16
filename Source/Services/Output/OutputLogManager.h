#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Output
{

enum class OutputCategory
{
    Build,
    CMake,
    Runner,
    LanguageServer,
    General
};

[[nodiscard]] constexpr std::string_view to_string(OutputCategory category) noexcept
{
    switch (category)
    {
    case OutputCategory::Build: return "Build";
    case OutputCategory::CMake: return "CMake";
    case OutputCategory::Runner: return "Runner";
    case OutputCategory::LanguageServer: return "Language Server";
    case OutputCategory::General: return "General";
    }
    return "General";
}

class OutputLogManager
{
public:
    [[nodiscard]] static OutputLogManager& instance();

    void append_line(OutputCategory category, std::string_view line);
    void append_text(OutputCategory category, std::string_view text);
    void clear(OutputCategory category);
    void clear_all();

    [[nodiscard]] std::vector<std::string> get_lines(OutputCategory category) const;
    [[nodiscard]] std::size_t get_line_count(OutputCategory category) const;
    [[nodiscard]] std::string get_combined_text(OutputCategory category) const;

    using LogListener = std::function<void(OutputCategory, std::string_view)>;
    void add_listener(LogListener listener);

private:
    OutputLogManager();
    mutable std::mutex m_mutex;
    std::unordered_map<OutputCategory, std::deque<std::string>> m_logs;
    std::vector<LogListener> m_listeners;
    static constexpr std::size_t max_lines_per_category = 2000;
};

} // namespace Zenvra::Services::Output
