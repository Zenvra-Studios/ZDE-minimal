#pragma once

#include "InstallationStore.h"

#include <filesystem>
#include <string>

namespace Zenvra::Services::Installation::Deployment
{

class DeploymentManager
{
public:
    explicit DeploymentManager(InstallationStore& store);

    [[nodiscard]] bool deploy(
        const std::string& server_id,
        const std::string& version,
        const std::string& language_id,
        const std::filesystem::path& extracted_dir,
        const std::filesystem::path& relative_binary_path);

    [[nodiscard]] bool undeploy(const std::string& server_id);

    [[nodiscard]] bool make_executable(const std::filesystem::path& file_path);

private:
    InstallationStore& m_store;
};

} // namespace Zenvra::Services::Installation::Deployment

