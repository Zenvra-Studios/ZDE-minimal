#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Services::Installation::Deployment
{

struct InstalledServerRecord
{
    std::string server_id;
    std::string version;
    std::string language_id;
    std::filesystem::path install_path;
    std::filesystem::path executable_path;
    std::string installed_at;
};

class InstallationStore
{
public:
    explicit InstallationStore(std::filesystem::path base_directory = {});

    [[nodiscard]] const std::filesystem::path& get_base_directory() const noexcept { return m_base_directory; }

    [[nodiscard]] std::vector<InstalledServerRecord> list_installed() const;

    [[nodiscard]] std::optional<InstalledServerRecord> get_record(const std::string& server_id) const;

    bool record_installation(const InstalledServerRecord& record);

    bool remove_record(const std::string& server_id);

    [[nodiscard]] std::filesystem::path get_server_directory(const std::string& server_id) const;

private:
    std::filesystem::path m_base_directory;
};

} // namespace Zenvra::Services::Installation::Deployment

