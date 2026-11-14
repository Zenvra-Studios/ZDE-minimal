#pragma once

#include "Language/Protocol/LspTypes.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Language::Features
{

class DiagnosticsService
{
public:
    static DiagnosticsService& instance() noexcept;

    using DiagnosticsListener = std::function<void(const std::string& uri, const std::vector<Zenvra::Language::Protocol::Diagnostic>&)>;

    void update_diagnostics(const std::string& uri, std::vector<Zenvra::Language::Protocol::Diagnostic> diagnostics);
    [[nodiscard]] std::vector<Zenvra::Language::Protocol::Diagnostic> get_diagnostics(const std::string& uri) const;
    void clear_diagnostics(const std::string& uri);
    void clear_all();

    void set_listener(DiagnosticsListener listener);

private:
    DiagnosticsService() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<Zenvra::Language::Protocol::Diagnostic>> m_cache;
    DiagnosticsListener m_listener;
};

} // namespace Zenvra::Services::Language::Features

