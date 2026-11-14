#pragma once

#include "Source/Services/Installation/Catalog/ServerCatalog.h"

#include <filesystem>
#include <functional>
#include <string>
#include <future>

namespace Zenvra::Services::Installation::Providers
{

struct InstallResult
{
    bool success{false};
    std::string error_message;
    std::filesystem::path installed_executable;
};

using InstallProgressCallback = std::function<void(float percentage, std::string status)>;

class IInstallProvider
{
public:
    virtual ~IInstallProvider() = default;

    [[nodiscard]] virtual std::string get_provider_type() const = 0;

    [[nodiscard]] virtual bool is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const = 0;

    virtual std::future<InstallResult> install_async(
        const Catalog::CatalogInstallation& install_spec,
        const std::filesystem::path& target_directory,
        InstallProgressCallback progress_callback = nullptr) = 0;
};

} // namespace Zenvra::Services::Installation::Providers

