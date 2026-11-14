#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Services::Installation::Catalog
{

struct CatalogArtifact
{
    std::string platform;       // "windows", "linux", "macos"
    std::string architecture;   // "x64", "arm64"
    std::string provider_type;  // "github_release", "direct_download", "system_package", "custom"
    std::string url_or_package;
    std::string sha256;
    std::string executable_relative_path;
};

struct CatalogInstallation
{
    std::string provider;
    std::string download_url;
    std::string sha256;
    std::string executable;
    std::string package_manager;
    std::string package_name;
};

struct ServerCatalogEntry
{
    std::string id;             // e.g. "clangd", "rust-analyzer", "pyright"
    std::string name;           // "Clangd C/C++ Language Server"
    std::string version;        // "18.1.3"
    std::string language_id;    // "cpp"
    std::string license;        // "Apache-2.0 WITH LLVM-exception"
    std::vector<CatalogArtifact> artifacts;

    [[nodiscard]] std::optional<CatalogArtifact> find_artifact_for_current_platform() const;
    [[nodiscard]] nlohmann::json to_json() const;
    static std::optional<ServerCatalogEntry> from_json(const nlohmann::json& j);
};

} // namespace Zenvra::Services::Installation::Catalog

