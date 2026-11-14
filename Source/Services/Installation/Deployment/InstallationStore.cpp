#include "InstallationStore.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Zenvra::Services::Installation::Deployment
{

InstallationStore::InstallationStore(std::filesystem::path base_directory)
    : m_base_directory(std::move(base_directory))
{
    if (m_base_directory.empty())
    {
#if defined(_WIN32)
        const char* appdata = std::getenv("APPDATA");
        if (appdata)
            m_base_directory = std::filesystem::path(appdata) / "Zenvra" / "servers";
        else
            m_base_directory = std::filesystem::current_path() / ".zenvra" / "servers";
#else
        const char* home = std::getenv("HOME");
        if (home)
            m_base_directory = std::filesystem::path(home) / ".local" / "share" / "zenvra" / "servers";
        else
            m_base_directory = std::filesystem::current_path() / ".zenvra" / "servers";
#endif
    }
}

std::filesystem::path InstallationStore::get_server_directory(const std::string& server_id) const
{
    return m_base_directory / server_id;
}

std::vector<InstalledServerRecord> InstallationStore::list_installed() const
{
    std::vector<InstalledServerRecord> records;
    std::error_code ec;

    if (!std::filesystem::exists(m_base_directory, ec))
        return records;

    for (const auto& entry : std::filesystem::directory_iterator(m_base_directory, ec))
    {
        if (!entry.is_directory(ec))
            continue;

        std::filesystem::path meta_file = entry.path() / "zenvra-server.json";
        if (std::filesystem::exists(meta_file, ec))
        {
            std::ifstream file(meta_file);
            if (file.is_open())
            {
                try
                {
                    nlohmann::json j;
                    file >> j;
                    InstalledServerRecord r;
                    r.server_id = j.value("serverId", entry.path().filename().string());
                    r.version = j.value("version", "");
                    r.language_id = j.value("languageId", "");
                    r.install_path = entry.path();
                    r.executable_path = j.value("executablePath", "");
                    r.installed_at = j.value("installedAt", "");
                    records.push_back(std::move(r));
                }
                catch (...)
                {
                }
            }
        }
    }

    return records;
}

std::optional<InstalledServerRecord> InstallationStore::get_record(const std::string& server_id) const
{
    std::filesystem::path meta_file = m_base_directory / server_id / "zenvra-server.json";
    std::error_code ec;
    if (!std::filesystem::exists(meta_file, ec))
        return std::nullopt;

    std::ifstream file(meta_file);
    if (!file.is_open())
        return std::nullopt;

    try
    {
        nlohmann::json j;
        file >> j;
        InstalledServerRecord r;
        r.server_id = j.value("serverId", server_id);
        r.version = j.value("version", "");
        r.language_id = j.value("languageId", "");
        r.install_path = m_base_directory / server_id;
        r.executable_path = j.value("executablePath", "");
        r.installed_at = j.value("installedAt", "");
        return r;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool InstallationStore::record_installation(const InstalledServerRecord& record)
{
    std::filesystem::path dir = m_base_directory / record.server_id;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    nlohmann::json j;
    j["serverId"] = record.server_id;
    j["version"] = record.version;
    j["languageId"] = record.language_id;
    j["executablePath"] = record.executable_path.string();
    j["installedAt"] = record.installed_at;

    std::ofstream file(dir / "zenvra-server.json");
    if (!file.is_open())
        return false;

    file << j.dump(2);
    return true;
}

bool InstallationStore::remove_record(const std::string& server_id)
{
    std::filesystem::path dir = m_base_directory / server_id;
    std::error_code ec;
    return std::filesystem::remove_all(dir, ec) > 0;
}

} // namespace Zenvra::Services::Installation::Deployment

