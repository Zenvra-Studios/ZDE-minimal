#include "Services/Language/Documents/DocumentCoordinator.h"
#include "Services/Language/Registry/LanguageRegistry.h"

#include <algorithm>

namespace Zenvra::Services::Language::Documents
{

DocumentCoordinator& DocumentCoordinator::instance() noexcept
{
    static auto* coord = new DocumentCoordinator();
    return *coord;
}

std::string DocumentCoordinator::path_to_uri(const std::filesystem::path& path)
{
    std::string generic = path.generic_string();
#if defined(_WIN32)
    if (generic.size() >= 2 && generic[1] == ':')
    {
        return "file:///" + generic;
    }
#endif
    if (!generic.starts_with("/"))
    {
        return "file:///" + generic;
    }
    return "file://" + generic;
}

std::filesystem::path DocumentCoordinator::uri_to_path(std::string_view uri)
{
    if (uri.starts_with("file:///"))
    {
#if defined(_WIN32)
        // file:///C:/path -> C:/path
        return std::filesystem::path(uri.substr(8));
#else
        return std::filesystem::path(uri.substr(7));
#endif
    }
    else if (uri.starts_with("file://"))
    {
        return std::filesystem::path(uri.substr(7));
    }
    return std::filesystem::path(uri);
}

void DocumentCoordinator::on_opened(
    const std::string& uri, std::string_view filename, std::string_view content, std::string_view language_id)
{
    DocumentRecord record;
    DocumentEventCallback listener;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        record.uri = uri;
        record.file_path = uri_to_path(uri);
        record.version = 1;
        record.content = std::string(content);
        record.is_open = true;
        record.is_dirty = false;

        if (!language_id.empty())
        {
            record.language_id = std::string(language_id);
        }
        else
        {
            const auto detected = Registry::LanguageRegistry::instance().detect_language(record.file_path, content);
            record.language_id = detected.language_id;
        }

        m_documents[uri] = record;
        listener = m_opened_listener;
    }

    if (listener)
    {
        listener(record);
    }
}

void DocumentCoordinator::on_changed(
    const std::string& uri, std::string_view content, std::optional<std::int32_t> explicit_version)
{
    DocumentRecord record;
    DocumentEventCallback listener;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_documents.find(uri);
        if (it != m_documents.end())
        {
            it->second.content = std::string(content);
            if (explicit_version)
            {
                it->second.version = *explicit_version;
            }
            else
            {
                it->second.version++;
            }
            it->second.is_dirty = true;
            record = it->second;
            listener = m_changed_listener;
        }
    }

    if (listener)
    {
        listener(record);
    }
}

void DocumentCoordinator::on_saved(const std::string& uri)
{
    DocumentRecord record;
    DocumentEventCallback listener;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_documents.find(uri);
        if (it != m_documents.end())
        {
            it->second.is_dirty = false;
            record = it->second;
            listener = m_saved_listener;
        }
    }

    if (listener)
    {
        listener(record);
    }
}

void DocumentCoordinator::on_closed(const std::string& uri)
{
    DocumentRecord record;
    DocumentEventCallback listener;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_documents.find(uri);
        if (it != m_documents.end())
        {
            it->second.is_open = false;
            record = it->second;
            listener = m_closed_listener;
            m_documents.erase(it);
        }
    }

    if (listener)
    {
        listener(record);
    }
}

std::optional<DocumentRecord> DocumentCoordinator::get_document(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_documents.find(uri);
    if (it != m_documents.end()) return it->second;
    return std::nullopt;
}

bool DocumentCoordinator::is_open(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_documents.find(uri);
    return it != m_documents.end() && it->second.is_open;
}

std::int32_t DocumentCoordinator::get_version(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_documents.find(uri);
    if (it != m_documents.end()) return it->second.version;
    return 1;
}

std::vector<std::string> DocumentCoordinator::get_open_uris() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    result.reserve(m_documents.size());
    for (const auto& [uri, doc] : m_documents)
    {
        if (doc.is_open)
        {
            result.push_back(uri);
        }
    }
    return result;
}

void DocumentCoordinator::set_on_opened_listener(DocumentEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_opened_listener = std::move(cb);
}

void DocumentCoordinator::set_on_changed_listener(DocumentEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_changed_listener = std::move(cb);
}

void DocumentCoordinator::set_on_saved_listener(DocumentEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_saved_listener = std::move(cb);
}

void DocumentCoordinator::set_on_closed_listener(DocumentEventCallback cb)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed_listener = std::move(cb);
}

} // namespace Zenvra::Services::Language::Documents

