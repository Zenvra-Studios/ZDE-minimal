#include "Services/Toolchain/ToolchainManager.h"

namespace Zenvra::Services::Toolchain
{

ToolchainManager& ToolchainManager::instance()
{
    static ToolchainManager s_instance;
    return s_instance;
}

void ToolchainManager::scan_toolchains(const std::filesystem::path& workspace_root)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_toolchains = ToolchainDetector::detect_all(workspace_root);
}

const IToolchain* ToolchainManager::get_toolchain(ToolchainType type) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& tc : m_toolchains)
    {
        if (tc && tc->type() == type && tc->is_available())
        {
            return tc.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<IToolchain>>& ToolchainManager::get_all_toolchains() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_toolchains;
}

} // namespace Zenvra::Services::Toolchain
