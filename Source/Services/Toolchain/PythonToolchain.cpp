#include "Services/Toolchain/PythonToolchain.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace Zenvra::Services::Toolchain
{

bool PythonToolchain::detect(const std::filesystem::path& workspace_root)
{
    std::error_code ec;

    // 1. Virtual environments in workspace
    if (!workspace_root.empty())
    {
        for (const auto& venv_name : {".venv", "venv", "env", ".env"})
        {
            const auto venv_path = workspace_root / venv_name;
#if defined(_WIN32)
            const auto py_exe = venv_path / "Scripts" / "python.exe";
#else
            const auto py_exe = venv_path / "bin" / "python";
#endif
            if (std::filesystem::exists(py_exe, ec))
            {
                m_executable = py_exe;
                m_name = "Python (virtualenv: " + std::string(venv_name) + ")";
                m_version = "Virtual Environment";
                m_source = ToolchainSource::VirtualEnv;
                m_available = true;
                return true;
            }
        }
    }

    // 2. System PATH
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"python", L".exe", MAX_PATH, buf, nullptr) > 0 ||
        SearchPathW(nullptr, L"python3", L".exe", MAX_PATH, buf, nullptr) > 0)
    {
        m_executable = std::filesystem::path(buf);
        m_name = "Python (System)";
        m_version = "Available";
        m_source = ToolchainSource::System;
        m_available = true;
        return true;
    }
#else
    m_executable = "python3";
    m_name = "Python (System)";
    m_version = "Available";
    m_source = ToolchainSource::System;
    m_available = true;
    return true;
#endif

    m_available = false;
    return false;
}

} // namespace Zenvra::Services::Toolchain
