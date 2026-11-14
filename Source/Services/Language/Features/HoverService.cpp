#include "Services/Language/Features/HoverService.h"

namespace Zenvra::Services::Language::Features
{

HoverService& HoverService::instance() noexcept
{
    static auto* svc = new HoverService();
    return *svc;
}

void HoverService::request_hover(
    const std::string& uri,
    std::string_view filename,
    const Zenvra::Language::Protocol::Position& position,
    std::string_view line_text,
    HoverCallback callback)
{
    if (m_provider)
    {
        m_provider(uri, filename, position, line_text, std::move(callback));
    }
    else if (callback)
    {
        callback(std::nullopt);
    }
}

} // namespace Zenvra::Services::Language::Features

