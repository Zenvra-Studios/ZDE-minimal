#include "Services/Installation/Catalog/CatalogLoader.h"

#include <fstream>
#include <iostream>

namespace Zenvra::Services::Installation::Catalog
{

std::optional<CatalogArtifact> ServerCatalogEntry::find_artifact_for_current_platform() const
{
    const std::string cur_plat = CatalogLoader::get_current_platform();
    const std::string cur_arch = CatalogLoader::get_current_architecture();

    for (const auto& art : artifacts)
    {
        if (art.platform == cur_plat && (art.architecture.empty() || art.architecture == cur_arch))
        {
            return art;
        }
    }
    return std::nullopt;
}

nlohmann::json ServerCatalogEntry::to_json() const
{
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["version"] = version;
    j["languageId"] = language_id;
    j["license"] = license;

    nlohmann::json arts = nlohmann::json::array();
    for (const auto& a : artifacts)
    {
        arts.push_back({
            {"platform", a.platform},
            {"architecture", a.architecture},
            {"providerType", a.provider_type},
            {"urlOrPackage", a.url_or_package},
            {"sha256", a.sha256},
            {"executableRelativePath", a.executable_relative_path}
        });
    }
    j["artifacts"] = arts;
    return j;
}

std::optional<ServerCatalogEntry> ServerCatalogEntry::from_json(const nlohmann::json& j)
{
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string())
    {
        return std::nullopt;
    }

    ServerCatalogEntry e;
    e.id = j["id"].get<std::string>();
    if (j.contains("name") && j["name"].is_string()) e.name = j["name"].get<std::string>();
    if (j.contains("version") && j["version"].is_string()) e.version = j["version"].get<std::string>();
    if (j.contains("languageId") && j["languageId"].is_string()) e.language_id = j["languageId"].get<std::string>();
    if (j.contains("license") && j["license"].is_string()) e.license = j["license"].get<std::string>();

    if (j.contains("artifacts") && j["artifacts"].is_array())
    {
        for (const auto& aj : j["artifacts"])
        {
            if (aj.is_object())
            {
                CatalogArtifact a;
                if (aj.contains("platform") && aj["platform"].is_string()) a.platform = aj["platform"].get<std::string>();
                if (aj.contains("architecture") && aj["architecture"].is_string()) a.architecture = aj["architecture"].get<std::string>();
                if (aj.contains("providerType") && aj["providerType"].is_string()) a.provider_type = aj["providerType"].get<std::string>();
                if (aj.contains("urlOrPackage") && aj["urlOrPackage"].is_string()) a.url_or_package = aj["urlOrPackage"].get<std::string>();
                if (aj.contains("sha256") && aj["sha256"].is_string()) a.sha256 = aj["sha256"].get<std::string>();
                if (aj.contains("executableRelativePath") && aj["executableRelativePath"].is_string()) a.executable_relative_path = aj["executableRelativePath"].get<std::string>();
                e.artifacts.push_back(std::move(a));
            }
        }
    }
    return e;
}

std::string CatalogLoader::get_current_platform()
{
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string CatalogLoader::get_current_architecture()
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "x64";
#endif
}

CatalogLoader& CatalogLoader::instance() noexcept
{
    static auto* loader = new CatalogLoader();
    return *loader;
}

CatalogLoader::CatalogLoader()
{
    register_builtin_catalog_entries();
}

void CatalogLoader::load_from_directory(const std::filesystem::path& dir_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir_path, ec) || !std::filesystem::is_directory(dir_path, ec))
    {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec))
    {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".json")
        {
            std::ifstream file(entry.path());
            if (file)
            {
                try
                {
                    nlohmann::json j;
                    file >> j;
                    if (auto cat = ServerCatalogEntry::from_json(j))
                    {
                        register_entry(std::move(*cat));
                    }
                }
                catch (...)
                {
                }
            }
        }
    }
}

bool CatalogLoader::register_entry(ServerCatalogEntry entry)
{
    if (entry.id.empty()) return false;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_entries[entry.id] = std::move(entry);
    return true;
}

std::optional<ServerCatalogEntry> CatalogLoader::find_entry(std::string_view server_id) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_entries.find(std::string(server_id));
    if (it != m_entries.end()) return it->second;
    return std::nullopt;
}

std::optional<ServerCatalogEntry> CatalogLoader::find_for_language(std::string_view language_id) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& [_, entry] : m_entries)
    {
        if (entry.language_id == language_id)
        {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<ServerCatalogEntry> CatalogLoader::get_all_entries() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<ServerCatalogEntry> result;
    result.reserve(m_entries.size());
    for (const auto& [_, e] : m_entries)
    {
        result.push_back(e);
    }
    return result;
}

void CatalogLoader::clear() noexcept
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_entries.clear();
}

void CatalogLoader::register_builtin_catalog_entries()
{
    // clangd
    {
        ServerCatalogEntry clangd;
        clangd.id = "clangd";
        clangd.name = "Clangd C/C++ Language Server";
        clangd.version = "18.1.3";
        clangd.language_id = "cpp";
        clangd.license = "Apache-2.0 WITH LLVM-exception";
        clangd.artifacts = {
            {
                .platform = "windows",
                .architecture = "x64",
                .provider_type = "github_release",
                .url_or_package = "clangd/clangd",
                .sha256 = "",
                .executable_relative_path = "bin/clangd.exe"
            },
            {
                .platform = "linux",
                .architecture = "x64",
                .provider_type = "github_release",
                .url_or_package = "clangd/clangd",
                .sha256 = "",
                .executable_relative_path = "bin/clangd"
            },
            {
                .platform = "macos",
                .architecture = "arm64",
                .provider_type = "github_release",
                .url_or_package = "clangd/clangd",
                .sha256 = "",
                .executable_relative_path = "bin/clangd"
            }
        };
        register_entry(std::move(clangd));
    }

    // rust-analyzer
    {
        ServerCatalogEntry ra;
        ra.id = "rust-analyzer";
        ra.name = "Rust Analyzer";
        ra.version = "2024-04-22";
        ra.language_id = "rust";
        ra.license = "MIT OR Apache-2.0";
        ra.artifacts = {
            {
                .platform = "windows",
                .architecture = "x64",
                .provider_type = "github_release",
                .url_or_package = "rust-lang/rust-analyzer",
                .sha256 = "",
                .executable_relative_path = "rust-analyzer.exe"
            },
            {
                .platform = "linux",
                .architecture = "x64",
                .provider_type = "github_release",
                .url_or_package = "rust-lang/rust-analyzer",
                .sha256 = "",
                .executable_relative_path = "rust-analyzer"
            }
        };
        register_entry(std::move(ra));
    }

    // jdtls (Eclipse JDT Language Server)
    {
        ServerCatalogEntry jdtls;
        jdtls.id = "jdtls";
        jdtls.name = "Eclipse JDT Language Server";
        jdtls.version = "1.34.0";
        jdtls.language_id = "java";
        jdtls.license = "EPL-2.0";
        jdtls.artifacts = {
            {
                .platform = "windows",
                .architecture = "x64",
                .provider_type = "direct_download",
                .url_or_package = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz",
                .sha256 = "",
                .executable_relative_path = "bin/jdtls.bat"
            },
            {
                .platform = "linux",
                .architecture = "x64",
                .provider_type = "direct_download",
                .url_or_package = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz",
                .sha256 = "",
                .executable_relative_path = "bin/jdtls"
            },
            {
                .platform = "macos",
                .architecture = "arm64",
                .provider_type = "direct_download",
                .url_or_package = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-latest.tar.gz",
                .sha256 = "",
                .executable_relative_path = "bin/jdtls"
            }
        };
        register_entry(std::move(jdtls));
    }
}

} // namespace Zenvra::Services::Installation::Catalog

