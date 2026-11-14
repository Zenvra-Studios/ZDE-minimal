#pragma once

#include "Services/LSP/Client/LspClient.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Zenvra::Services::LSP::Client
{

class LspSession
{
public:
    LspSession(std::string language_id, std::filesystem::path workspace_root, std::unique_ptr<LspClient> client);

    [[nodiscard]] const std::string& get_language_id() const noexcept { return m_language_id; }
    [[nodiscard]] const std::filesystem::path& get_workspace_root() const noexcept { return m_workspace_root; }
    [[nodiscard]] LspClient* get_client() noexcept { return m_client.get(); }

    void add_document(const std::string& uri);
    void remove_document(const std::string& uri);
    [[nodiscard]] bool has_documents() const noexcept;
    [[nodiscard]] std::size_t document_count() const noexcept;

    void shutdown();

private:
    std::string m_language_id;
    std::filesystem::path m_workspace_root;
    std::unique_ptr<LspClient> m_client;
    std::unordered_set<std::string> m_open_uris;
    mutable std::mutex m_mutex;
};

} // namespace Zenvra::Services::LSP::Client

