#include "Services/Language/Features/CompletionService.h"

namespace Zenvra::Services::Language::Features
{

CompletionService& CompletionService::instance() noexcept
{
    static auto* svc = new CompletionService();
    return *svc;
}

void CompletionService::request_completion(
    const std::string& uri,
    std::string_view filename,
    const Zenvra::Language::Protocol::Position& position,
    std::string_view line_text,
    CompletionCallback callback,
    std::optional<char> trigger_character)
{
    if (m_provider)
    {
        m_provider(uri, filename, position, line_text, std::move(callback), trigger_character);
    }
    else if (callback)
    {
        callback({});
    }
}

std::vector<Zenvra::Language::Protocol::CompletionItem> CompletionService::get_builtin_templates(std::string_view filename)
{
    if (instance().m_template_provider)
    {
        return instance().m_template_provider(filename);
    }
    return {};
}

} // namespace Zenvra::Services::Language::Features

