#include "Services/Toolchain/ToolchainDetector.h"

#include "Services/Toolchain/CppToolchain.h"
#include "Services/Toolchain/FreePascalToolchain.h"
#include "Services/Toolchain/JavaToolchain.h"
#include "Services/Toolchain/PythonToolchain.h"
#include "Services/Toolchain/RustToolchain.h"

namespace Zenvra::Services::Toolchain
{

std::vector<std::unique_ptr<IToolchain>> ToolchainDetector::detect_all(
    const std::filesystem::path& workspace_root)
{
    std::vector<std::unique_ptr<IToolchain>> toolchains;

    auto cpp = std::make_unique<CppToolchain>();
    if (cpp->detect(workspace_root)) toolchains.push_back(std::move(cpp));

    auto rust = std::make_unique<RustToolchain>();
    if (rust->detect(workspace_root)) toolchains.push_back(std::move(rust));

    auto python = std::make_unique<PythonToolchain>();
    if (python->detect(workspace_root)) toolchains.push_back(std::move(python));

    auto java = std::make_unique<JavaToolchain>();
    if (java->detect(workspace_root)) toolchains.push_back(std::move(java));

    auto pascal = std::make_unique<FreePascalToolchain>();
    if (pascal->detect(workspace_root)) toolchains.push_back(std::move(pascal));

    return toolchains;
}

} // namespace Zenvra::Services::Toolchain
