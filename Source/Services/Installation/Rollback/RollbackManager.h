#pragma once

#include <filesystem>
#include <string>

namespace Zenvra::Services::Installation::Rollback
{

class RollbackManager
{
public:
    RollbackManager() = default;

    [[nodiscard]] bool create_backup(
        const std::string& operation_id,
        const std::filesystem::path& target_dir);

    [[nodiscard]] bool restore_backup(
        const std::string& operation_id,
        const std::filesystem::path& target_dir);

    void cleanup_backup(const std::string& operation_id);

private:
    [[nodiscard]] std::filesystem::path get_backup_path(const std::string& operation_id) const;
};

} // namespace Zenvra::Services::Installation::Rollback

