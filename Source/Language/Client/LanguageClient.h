#pragma once

#include "Language/Client/DocumentSyncManager.h"
#include "Language/Client/ILanguageClient.h"
#include "Language/Transport/ILspTransport.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Zenvra::Language::Client
{

class LanguageClient final : public ILanguageClient
{
public:
    LanguageClient(
        std::string language_id,
        std::unique_ptr<Transport::ILspTransport> transport,
        std::filesystem::path workspace_root = {});
    ~LanguageClient() override;

    bool start() override;
    void shutdown() override;
    void exit() override;

    [[nodiscard]] ClientState get_state() const noexcept override;
    [[nodiscard]] bool is_active() const noexcept override;
    [[nodiscard]] std::vector<std::string> get_semantic_token_legend() const noexcept override { return m_semantic_token_legend; }
    [[nodiscard]] const std::string& get_language_id() const noexcept { return m_language_id; }

    // Document sync
    void did_open(const std::string& uri, std::string_view language_id, int version, std::string_view text) override;
    void did_change(const std::string& uri, int version, std::string_view text) override;
    void did_save(const std::string& uri) override;
    void did_close(const std::string& uri) override;
    [[nodiscard]] bool is_document_open(const std::string& uri) const noexcept override;

    // LSP features
    void request_completion(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::vector<Protocol::CompletionItem>)> callback,
        std::optional<char> trigger_character = std::nullopt) override;

    void request_hover(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::optional<Protocol::Hover>)> callback) override;

    void request_definition(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::vector<Protocol::Location>)> callback) override;

    void request_signature_help(
        const std::string& uri,
        const Protocol::Position& pos,
        std::function<void(std::optional<Protocol::SignatureHelp>)> callback) override;

    void request_semantic_tokens(
        const std::string& uri,
        std::function<void(std::optional<Protocol::SemanticTokens>)> callback) override;

    void set_diagnostics_handler(
        std::function<void(const std::string& uri, const std::vector<Protocol::Diagnostic>&)> handler) override;

public:
    void set_initialization_options(nlohmann::json options);

private:
    void on_message_received(std::string_view json_string);
    int64_t send_request(std::string_view method, nlohmann::json params, std::function<void(const nlohmann::json&)> callback);
    void send_notification(std::string_view method, nlohmann::json params);

    std::string m_language_id;
    std::unique_ptr<Transport::ILspTransport> m_transport;
    std::filesystem::path m_workspace_root;
    DocumentSyncManager m_sync_manager;
    nlohmann::json m_initialization_options;
    std::vector<std::string> m_semantic_token_legend;

    std::atomic<ClientState> m_state{ClientState::Uninitialized};
    int64_t m_next_request_id{1};
    std::mutex m_request_mutex;
    std::unordered_map<int64_t, std::function<void(const nlohmann::json&)>> m_pending_requests;
    std::vector<std::string> m_queued_packets;

    std::function<void(const std::string&, const std::vector<Protocol::Diagnostic>&)> m_diagnostics_handler;
};

} // namespace Zenvra::Language::Client
