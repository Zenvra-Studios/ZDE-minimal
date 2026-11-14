#include "Services/LSP/Client/LspSession.h"

namespace Zenvra::Services::LSP::Client
{

LspSession::LspSession(
    std::string language_id,
    std::filesystem::path workspace_root,
    std::unique_ptr<LspClient> client)
    : m_language_id(std::move(language_id))
    , m_workspace_root(std::move(workspace_root))
    , m_client(std::move(client))
{
}

void LspSession::add_document(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_open_uris.insert(uri);
}

void LspSession::remove_document(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_open_uris.erase(uri);
}

bool LspSession::has_documents() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_open_uris.empty();
}

std::size_t LspSession::document_count() const noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_open_uris.size();
}

void LspSession::shutdown()
{
    if (m_client)
    {
        m_client->shutdown();
    }
}

} // namespace Zenvra::Services::LSP::Client

