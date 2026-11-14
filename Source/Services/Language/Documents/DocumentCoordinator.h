#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Language::Documents
{

struct DocumentRecord
{
    std::string uri;
    std::filesystem::path file_path;
    std::string language_id;
    std::int32_t version{1};
    std::string content;
    bool is_open{false};
    bool is_dirty{false};
};

class DocumentCoordinator
{
public:
    static DocumentCoordinator& instance() noexcept;

    using DocumentEventCallback = std::function<void(const DocumentRecord&)>;

    void on_opened(const std::string& uri, std::string_view filename, std::string_view content, std::string_view language_id = {});
    void on_changed(const std::string& uri, std::string_view content, std::optional<std::int32_t> explicit_version = std::nullopt);
    void on_saved(const std::string& uri);
    void on_closed(const std::string& uri);

    [[nodiscard]] std::optional<DocumentRecord> get_document(const std::string& uri) const;
    [[nodiscard]] bool is_open(const std::string& uri) const;
    [[nodiscard]] std::int32_t get_version(const std::string& uri) const;
    [[nodiscard]] std::vector<std::string> get_open_uris() const;

    void set_on_opened_listener(DocumentEventCallback cb);
    void set_on_changed_listener(DocumentEventCallback cb);
    void set_on_saved_listener(DocumentEventCallback cb);
    void set_on_closed_listener(DocumentEventCallback cb);

    static std::string path_to_uri(const std::filesystem::path& path);
    static std::filesystem::path uri_to_path(std::string_view uri);

private:
    DocumentCoordinator() = default;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, DocumentRecord> m_documents; // URI -> DocumentRecord

    DocumentEventCallback m_opened_listener;
    DocumentEventCallback m_changed_listener;
    DocumentEventCallback m_saved_listener;
    DocumentEventCallback m_closed_listener;
};

} // namespace Zenvra::Services::Language::Documents

