#pragma once

#include "Language/Protocol/LspTypes.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Services::Language::Features
{

class CompletionService
{
public:
    static CompletionService& instance() noexcept;

    using CompletionCallback = std::function<void(std::vector<Zenvra::Language::Protocol::CompletionItem>)>;

    using CompletionProvider = std::function<void(
        const std::string& uri,
        std::string_view filename,
        const Zenvra::Language::Protocol::Position& position,
        std::string_view line_text,
        CompletionCallback callback,
        std::optional<char> trigger_character)>;

    using TemplateProvider = std::function<std::vector<Zenvra::Language::Protocol::CompletionItem>(std::string_view filename)>;

    void set_provider(CompletionProvider provider) noexcept { m_provider = std::move(provider); }
    void set_template_provider(TemplateProvider provider) noexcept { m_template_provider = std::move(provider); }

    void request_completion(
        const std::string& uri,
        std::string_view filename,
        const Zenvra::Language::Protocol::Position& position,
        std::string_view line_text,
        CompletionCallback callback,
        std::optional<char> trigger_character = std::nullopt);

    static std::vector<Zenvra::Language::Protocol::CompletionItem> get_builtin_templates(std::string_view filename);

private:
    CompletionService() = default;
    CompletionProvider m_provider;
    TemplateProvider m_template_provider;
};

} // namespace Zenvra::Services::Language::Features

