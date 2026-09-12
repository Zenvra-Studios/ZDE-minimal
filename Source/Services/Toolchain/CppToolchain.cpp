#include "Services/Toolchain/CppToolchain.h"

#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Toolchain
{

bool CppToolchain::detect(const std::filesystem::path& /*workspace_root*/)
{
    const std::vector<std::pair<std::string, std::string>> candidates = {
        {"clang++", "Clang++"},
        {"clang", "Clang"},
        {"g++", "GCC (g++)"},
        {"gcc", "GCC"},
        {"cl", "MSVC (cl)"}
    };

    for (const auto& [cmd, title] : candidates)
    {
#if defined(_WIN32)
        const std::wstring wcmd(cmd.begin(), cmd.end());
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, wcmd.c_str(), L".exe", MAX_PATH, buf, nullptr) > 0)
        {
            m_executable = std::filesystem::path(buf);
            m_name = title;
            m_version = "Available";
            m_source = ToolchainSource::System;
            m_available = true;
            return true;
        }
#else
        m_executable = cmd;
        m_name = title;
        m_version = "Available";
        m_source = ToolchainSource::System;
        m_available = true;
        return true;
#endif
    }

    m_available = false;
    return false;
}

} // namespace Zenvra::Services::Toolchain
