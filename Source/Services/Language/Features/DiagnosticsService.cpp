#include "Services/Language/Features/DiagnosticsService.h"

namespace Zenvra::Services::Language::Features
{

DiagnosticsService& DiagnosticsService::instance() noexcept
{
    static auto* svc = new DiagnosticsService();
    return *svc;
}

void DiagnosticsService::update_diagnostics(
    const std::string& uri, std::vector<Zenvra::Language::Protocol::Diagnostic> diagnostics)
{
    DiagnosticsListener listener;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache[uri] = diagnostics;
        listener = m_listener;
    }
    if (listener)
    {
        listener(uri, diagnostics);
    }
}

std::vector<Zenvra::Language::Protocol::Diagnostic> DiagnosticsService::get_diagnostics(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cache.find(uri);
    if (it != m_cache.end()) return it->second;
    return {};
}

void DiagnosticsService::clear_diagnostics(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.erase(uri);
}

void DiagnosticsService::clear_all()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
}

void DiagnosticsService::set_listener(DiagnosticsListener listener)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listener = std::move(listener);
}

} // namespace Zenvra::Services::Language::Features

