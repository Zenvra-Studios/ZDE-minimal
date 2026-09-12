#pragma once

#include "Services/Toolchain/Toolchain.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Zenvra::Services::Toolchain
{

class ToolchainDetector
{
public:
    [[nodiscard]] static std::vector<std::unique_ptr<IToolchain>> detect_all(
        const std::filesystem::path& workspace_root = {});
};

} // namespace Zenvra::Services::Toolchain
