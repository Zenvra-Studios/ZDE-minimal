#include "Services/Toolchain/JavaToolchain.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Toolchain
{

bool JavaToolchain::detect(const std::filesystem::path& /*workspace_root*/)
{
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"javac", L".exe", MAX_PATH, buf, nullptr) > 0 ||
        SearchPathW(nullptr, L"java", L".exe", MAX_PATH, buf, nullptr) > 0)
    {
        m_executable = std::filesystem::path(buf);
        m_name = "Java (JDK)";
        m_version = "Available";
        m_source = ToolchainSource::System;
        m_available = true;
        return true;
    }
#else
    m_executable = "java";
    m_name = "Java (JDK)";
    m_version = "Available";
    m_source = ToolchainSource::System;
    m_available = true;
    return true;
#endif

    m_available = false;
    return false;
}

} // namespace Zenvra::Services::Toolchain
