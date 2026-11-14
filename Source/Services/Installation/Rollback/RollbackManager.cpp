#include "RollbackManager.h"

namespace Zenvra::Services::Installation::Rollback
{

std::filesystem::path RollbackManager::get_backup_path(const std::string& operation_id) const
{
    return std::filesystem::temp_directory_path() / "zenvra_backup" / operation_id;
}

bool RollbackManager::create_backup(
    const std::string& operation_id,
    const std::filesystem::path& target_dir)
{
    std::error_code ec;
    if (!std::filesystem::exists(target_dir, ec))
        return true; // Nothing to backup

    std::filesystem::path backup = get_backup_path(operation_id);
    if (std::filesystem::exists(backup, ec))
        std::filesystem::remove_all(backup, ec);

    std::filesystem::create_directories(backup.parent_path(), ec);
    std::filesystem::copy(target_dir, backup, std::filesystem::copy_options::recursive, ec);
    return !ec;
}

bool RollbackManager::restore_backup(
    const std::string& operation_id,
    const std::filesystem::path& target_dir)
{
    std::error_code ec;
    std::filesystem::path backup = get_backup_path(operation_id);
    if (!std::filesystem::exists(backup, ec))
        return false;

    if (std::filesystem::exists(target_dir, ec))
        std::filesystem::remove_all(target_dir, ec);

    std::filesystem::create_directories(target_dir.parent_path(), ec);
    std::filesystem::copy(backup, target_dir, std::filesystem::copy_options::recursive, ec);
    return !ec;
}

void RollbackManager::cleanup_backup(const std::string& operation_id)
{
    std::error_code ec;
    std::filesystem::path backup = get_backup_path(operation_id);
    if (std::filesystem::exists(backup, ec))
        std::filesystem::remove_all(backup, ec);
}

} // namespace Zenvra::Services::Installation::Rollback

