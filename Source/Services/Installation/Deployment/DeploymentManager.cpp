#include "DeploymentManager.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Zenvra::Services::Installation::Deployment
{

namespace
{
std::string get_current_iso_time()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}
} // namespace

DeploymentManager::DeploymentManager(InstallationStore& store)
    : m_store(store)
{
}

bool DeploymentManager::deploy(
    const std::string& server_id,
    const std::string& version,
    const std::string& language_id,
    const std::filesystem::path& extracted_dir,
    const std::filesystem::path& relative_binary_path)
{
    std::error_code ec;
    std::filesystem::path target_dir = m_store.get_server_directory(server_id);

    // Remove existing target if any
    if (std::filesystem::exists(target_dir, ec))
    {
        std::filesystem::remove_all(target_dir, ec);
    }

    // Atomic move or recursive copy
    std::filesystem::create_directories(target_dir.parent_path(), ec);
    std::filesystem::rename(extracted_dir, target_dir, ec);
    if (ec)
    {
        // Fallback to copy
        ec.clear();
        std::filesystem::copy(extracted_dir, target_dir, std::filesystem::copy_options::recursive, ec);
        if (ec)
            return false;
    }

    std::filesystem::path full_bin_path = target_dir / relative_binary_path;
    make_executable(full_bin_path);

    InstalledServerRecord rec;
    rec.server_id = server_id;
    rec.version = version;
    rec.language_id = language_id;
    rec.install_path = target_dir;
    rec.executable_path = full_bin_path;
    rec.installed_at = get_current_iso_time();

    return m_store.record_installation(rec);
}

bool DeploymentManager::undeploy(const std::string& server_id)
{
    return m_store.remove_record(server_id);
}

bool DeploymentManager::make_executable(const std::filesystem::path& file_path)
{
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec))
        return false;

#if !defined(_WIN32)
    auto perms = std::filesystem::status(file_path, ec).permissions();
    perms |= std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec;
    std::filesystem::permissions(file_path, perms, std::filesystem::perm_options::add, ec);
    return !ec;
#else
    return true;
#endif
}

} // namespace Zenvra::Services::Installation::Deployment

