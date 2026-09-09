#pragma once

#include "Plugins/Installer/PluginInstaller.h"

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::Plugins::Marketplace
{

struct MarketplacePluginEntry
{
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string publisher;
    std::string repository_url;
    std::string download_url;
    std::string icon_url;
    std::string sha256;
    std::string category = "general";
    std::vector<std::string> tags;
    std::string downloads = "100K";
    std::string rating = "5.0";
};

class MarketplaceClient
{
public:
    MarketplaceClient() = default;

    bool load_catalog_from_file(const std::filesystem::path& file_path);
    bool load_catalog_from_json(const nlohmann::json& j);

    [[nodiscard]] std::vector<MarketplacePluginEntry> search(std::string_view query) const;
    [[nodiscard]] std::vector<MarketplacePluginEntry> get_all_entries() const;
    [[nodiscard]] std::optional<MarketplacePluginEntry> find_entry(std::string_view id) const;

    Installer::InstallResult install_from_catalog(
        std::string_view plugin_id,
        Installer::PluginInstaller& installer,
        std::string_view current_zde_version = "0.1.0",
        std::function<void(std::string_view)> log_callback = nullptr);

    void ensure_default_catalog_if_empty();
    void clear() noexcept;

    void set_cache_dir(std::filesystem::path dir) { m_cache_dir = std::move(dir); }
    [[nodiscard]] const std::filesystem::path& get_cache_dir() const noexcept { return m_cache_dir; }

    [[nodiscard]] std::optional<std::filesystem::path> get_cached_icon(
        const std::string& plugin_id,
        const std::string& icon_url = "",
        std::function<void()> on_downloaded = nullptr) const;

private:
    mutable std::mutex m_mutex;
    std::vector<MarketplacePluginEntry> m_entries;
    std::filesystem::path m_cache_dir;
    mutable std::mutex m_icon_mutex;
    mutable std::vector<std::string> m_pending_downloads;
};

} // namespace Zenvra::Plugins::Marketplace
