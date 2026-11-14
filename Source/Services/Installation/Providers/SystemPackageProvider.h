#pragma once

#include "InstallProvider.h"

namespace Zenvra::Services::Installation::Providers
{

class SystemPackageProvider : public IInstallProvider
{
public:
    SystemPackageProvider() = default;

    [[nodiscard]] std::string get_provider_type() const override { return "system_package"; }

    [[nodiscard]] bool is_supported_on_current_platform(const Catalog::CatalogInstallation& install_spec) const override;

    std::future<InstallResult> install_async(
        const Catalog::CatalogInstallation& install_spec,
        const std::filesystem::path& target_directory,
        InstallProgressCallback progress_callback = nullptr) override;
};

} // namespace Zenvra::Services::Installation::Providers

