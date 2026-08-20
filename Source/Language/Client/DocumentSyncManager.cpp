#include "Language/Client/DocumentSyncManager.h"

namespace Zenvra::Language::Client
{

Protocol::JsonRpcNotification DocumentSyncManager::create_did_open(
    const std::string& uri,
    std::string_view language_id,
    int version,
    std::string_view text)
{
    m_documents[uri] = TrackedDocument{
        .uri = uri,
        .language_id = std::string(language_id),
        .version = version
    };

    return Protocol::JsonRpcNotification{
        .method = "textDocument/didOpen",
        .params = {
            {"textDocument", {
                {"uri", uri},
                {"languageId", std::string(language_id)},
                {"version", version},
                {"text", std::string(text)}
            }}
        }
    };
}

Protocol::JsonRpcNotification DocumentSyncManager::create_did_change(
    const std::string& uri,
    int version,
    std::string_view text)
{
    int next_ver = version;
    if (auto it = m_documents.find(uri); it != m_documents.end())
    {
        if (version <= it->second.version)
        {
            it->second.version++;
        }
        else
        {
            it->second.version = version;
        }
        next_ver = it->second.version;
    }

    return Protocol::JsonRpcNotification{
        .method = "textDocument/didChange",
        .params = {
            {"textDocument", {
                {"uri", uri},
                {"version", next_ver}
            }},
            {"contentChanges", nlohmann::json::array({
                {{"text", std::string(text)}}
            })}
        }
    };
}

Protocol::JsonRpcNotification DocumentSyncManager::create_did_save(
    const std::string& uri)
{
    return Protocol::JsonRpcNotification{
        .method = "textDocument/didSave",
        .params = {
            {"textDocument", {
                {"uri", uri}
            }}
        }
    };
}

Protocol::JsonRpcNotification DocumentSyncManager::create_did_close(
    const std::string& uri)
{
    m_documents.erase(uri);

    return Protocol::JsonRpcNotification{
        .method = "textDocument/didClose",
        .params = {
            {"textDocument", {
                {"uri", uri}
            }}
        }
    };
}

bool DocumentSyncManager::is_tracked(const std::string& uri) const noexcept
{
    return m_documents.contains(uri);
}

int DocumentSyncManager::get_version(const std::string& uri) const noexcept
{
    if (auto it = m_documents.find(uri); it != m_documents.end())
    {
        return it->second.version;
    }
    return 0;
}

} // namespace Zenvra::Language::Client
