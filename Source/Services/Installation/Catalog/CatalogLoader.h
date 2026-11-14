#pragma once

#include "Services/Installation/Catalog/ServerCatalog.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zenvra::Services::Installation::Catalog
{

class CatalogLoader
{
public:
    static CatalogLoader& instance() noexcept;

    void load_from_directory(const std::filesystem::path& dir_path);
    bool register_entry(ServerCatalogEntry entry);

    [[nodiscard]] std::optional<ServerCatalogEntry> find_entry(std::string_view server_id) const;
    [[nodiscard]] std::optional<ServerCatalogEntry> find_for_language(std::string_view language_id) const;
    [[nodiscard]] std::vector<ServerCatalogEntry> get_all_entries() const;

    [[nodiscard]] static std::string get_current_platform();
    [[nodiscard]] static std::string get_current_architecture();

    void clear() noexcept;

private:
    CatalogLoader();
    void register_builtin_catalog_entries();

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, ServerCatalogEntry> m_entries;
};

} // namespace Zenvra::Services::Installation::Catalog

