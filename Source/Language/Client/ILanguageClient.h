#pragma once

#include "Language/Protocol/LspTypes.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Language::Client
{

enum class ClientState
{
    Uninitialized,
    Initializing,
    Initialized,
    Active,
    Shutdown,
    Exit,
    Error
};

class ILanguageClient
{
public:
    virtual ~ILanguageClient() = default;

    virtual bool start() = 0;
    virtual void shutdown() = 0;
    virtual void exit() = 0;
    [[nodiscard]] virtual ClientState get_state() const noexcept = 0;
    [[nodiscard]] virtual bool is_active() const noexcept = 0;

    // Document sync
    virtual void did_open(const std::string& uri, std::string_view language_id, int version, std::string_view text) = 0;
    virtual void did_change(const std::string& uri, int version, std::string_view text) = 0;
    virtual void did_save(const std::string& uri) = 0;
    virtual void did_close(const std::string& uri) = 0;
    [[nodiscard]] virtual bool is_document_open(const std::string& uri) const noexcept = 0;

    // LSP features
    virtual void request_completion(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::vector<Protocol::CompletionItem>)> callback) = 0;

    virtual void request_hover(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::optional<Protocol::Hover>)> callback) = 0;

    virtual void request_definition(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::vector<Protocol::Location>)> callback) = 0;

    virtual void request_signature_help(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::optional<Protocol::SignatureHelp>)> callback) = 0;

    virtual void request_semantic_tokens(
        const std::string& uri,
        std::function<void(std::optional<Protocol::SemanticTokens>)> callback) = 0;

    virtual void set_diagnostics_handler(
        std::function<void(const std::string& uri, const std::vector<Protocol::Diagnostic>&)> handler) = 0;
};

} // namespace Zenvra::Language::Client
