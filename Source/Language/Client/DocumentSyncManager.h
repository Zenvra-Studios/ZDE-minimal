#pragma once

#include "Language/Protocol/LspMessage.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace Zenvra::Language::Client
{

struct TrackedDocument
{
    std::string uri;
    std::string language_id;
    int version = 1;
};

class DocumentSyncManager
{
public:
    DocumentSyncManager() = default;

    [[nodiscard]] Protocol::JsonRpcNotification create_did_open(
        const std::string& uri,
        std::string_view language_id,
        int version,
        std::string_view text);

    [[nodiscard]] Protocol::JsonRpcNotification create_did_change(
        const std::string& uri,
        int version,
        std::string_view text);

    [[nodiscard]] Protocol::JsonRpcNotification create_did_save(
        const std::string& uri);

    [[nodiscard]] Protocol::JsonRpcNotification create_did_close(
        const std::string& uri);

    [[nodiscard]] bool is_tracked(const std::string& uri) const noexcept;
    [[nodiscard]] int get_version(const std::string& uri) const noexcept;

private:
    std::unordered_map<std::string, TrackedDocument> m_documents;
};

} // namespace Zenvra::Language::Client
