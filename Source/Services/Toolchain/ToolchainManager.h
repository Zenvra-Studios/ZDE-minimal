#pragma once

#include "Services/Toolchain/Toolchain.h"
#include "Services/Toolchain/ToolchainDetector.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

namespace Zenvra::Services::Toolchain
{

class ToolchainManager
{
public:
    static ToolchainManager& instance();

    void scan_toolchains(const std::filesystem::path& workspace_root = {});

    [[nodiscard]] const IToolchain* get_toolchain(ToolchainType type) const;
    [[nodiscard]] const std::vector<std::unique_ptr<IToolchain>>& get_all_toolchains() const;

private:
    ToolchainManager() = default;
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<IToolchain>> m_toolchains;
};

} // namespace Zenvra::Services::Toolchain
