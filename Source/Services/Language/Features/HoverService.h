#pragma once

#include "Language/Protocol/LspTypes.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Services::Language::Features
{

class HoverService
{
public:
    static HoverService& instance() noexcept;

    using HoverCallback = std::function<void(std::optional<Zenvra::Language::Protocol::Hover>)>;

    using HoverProvider = std::function<void(
        const std::string& uri,
        std::string_view filename,
        const Zenvra::Language::Protocol::Position& position,
        std::string_view line_text,
        HoverCallback callback)>;

    void set_provider(HoverProvider provider) noexcept { m_provider = std::move(provider); }

    void request_hover(
        const std::string& uri,
        std::string_view filename,
        const Zenvra::Language::Protocol::Position& position,
        std::string_view line_text,
        HoverCallback callback);

private:
    HoverService() = default;
    HoverProvider m_provider;
};

} // namespace Zenvra::Services::Language::Features

