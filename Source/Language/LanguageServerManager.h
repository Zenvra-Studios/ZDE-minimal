#pragma once

#include "Language/Client/LanguageClient.h"
#include "Language/Registry/ServerRegistry.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Syntax/SemanticTokensManager.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Zenvra::Language
{

class LanguageServerManager
{
public:
    static LanguageServerManager& instance() noexcept;

    void set_workspace_root(std::filesystem::path root_path);
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_workspace_root; }

    /// Gets or creates (lazy starts) the language client for a given file extension/language.
    Client::ILanguageClient* get_or_start_client_for_file(std::string_view filename);

    /// Document sync lifecycle
    void on_document_opened(const std::string& uri, std::string_view filename, int version, std::string_view content);
    void on_document_changed(const std::string& uri, std::string_view filename, int version, std::string_view content);
    void on_document_saved(const std::string& uri, std::string_view filename);
    void on_document_closed(const std::string& uri, std::string_view filename);

    static std::vector<Protocol::CompletionItem> get_templates_for_filename(std::string_view filename);
    static std::vector<Protocol::CompletionItem> get_header_completions(std::string_view line_prefix, const std::filesystem::path& workspace_root = {});

    /// LSP queries (Powered purely by the real binary LSP e.g. clangd.exe)
    void request_completion(
        const std::string& uri,
        std::string_view filename,
        const Protocol::Position& pos,
        std::string_view line_text,
        std::function<void(std::vector<Protocol::CompletionItem>)> callback);

    void request_hover(
        const std::string& uri,
        std::string_view filename,
        const Protocol::Position& pos,
        std::string_view line_text,
        std::function<void(std::optional<Protocol::Hover>)> callback);

    void request_definition(
        const std::string& uri,
        std::string_view filename,
        const Protocol::Position& pos,
        std::function<void(std::vector<Protocol::Location>)> callback);

    void request_signature_help(
        const std::string& uri,
        std::string_view filename,
        const Protocol::Position& pos,
        std::string_view line_text,
        std::function<void(std::optional<Protocol::SignatureHelp>)> callback);

    void request_semantic_tokens(
        const std::string& uri,
        std::string_view filename,
        std::function<void(std::vector<Syntax::SemanticTokenSpan>)> callback = nullptr);

    /// Semantic Tokens manager access
    [[nodiscard]] Syntax::SemanticTokensManager& get_semantic_tokens_manager() noexcept { return m_semantic_tokens_manager; }
    [[nodiscard]] const Syntax::SemanticTokensManager& get_semantic_tokens_manager() const noexcept { return m_semantic_tokens_manager; }

    /// Diagnostics listener callback
    void set_diagnostics_callback(
        std::function<void(const std::string& uri, const std::vector<Protocol::Diagnostic>&)> callback);

    /// Gets cached diagnostics for a document URI
    [[nodiscard]] std::vector<Protocol::Diagnostic> get_diagnostics_for_document(const std::string& uri) const;

    /// Shutdown all active clients
    void shutdown_all();
    void stop_all() { shutdown_all(); }

private:
    LanguageServerManager();
    ~LanguageServerManager();

    std::filesystem::path m_workspace_root;
    Syntax::SemanticTokensManager m_semantic_tokens_manager;

    std::mutex m_clients_mutex;
    std::unordered_map<std::string, std::unique_ptr<Client::LanguageClient>> m_clients; // language_id -> client
    std::unordered_map<std::string, std::vector<Protocol::Diagnostic>> m_document_diagnostics; // uri -> diagnostics

    std::function<void(const std::string&, const std::vector<Protocol::Diagnostic>&)> m_diagnostics_callback;
};

} // namespace Zenvra::Language
