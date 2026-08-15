#include "Language/Toolchain/ToolchainDetector.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Zenvra::Language::Toolchain
{

namespace
{

bool file_exists(const std::filesystem::path& p)
{
    std::error_code ec;
    return std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec);
}

} // namespace

ToolchainDetector& ToolchainDetector::instance()
{
    static ToolchainDetector s_instance;
    return s_instance;
}

ToolchainDetector::ToolchainDetector()
{
    detect_environment();
}

const ToolchainInfo& ToolchainDetector::get_active_toolchain()
{
    if (!m_detected)
    {
        detect_environment();
    }
    return m_info;
}

void ToolchainDetector::refresh()
{
    detect_environment();
}

bool ToolchainDetector::has_valid_sdk() const
{
    return m_info.is_ready() && m_info.has_standard_headers;
}

std::string ToolchainDetector::get_status_bar_label() const
{
    if (m_info.is_ready())
    {
        return "[Toolchain: " + m_info.name + "]";
    }
    return "[!] No C++ SDK / Toolchain";
}

std::string ToolchainDetector::get_tooltip_guidance() const
{
    if (m_info.is_ready())
    {
        return "C++ Toolchain is configured: " + m_info.name + " (" + m_info.compiler_path.string() + ")";
    }
    return m_info.warning_message + "\n\nInstallation Guide:\n" + m_info.installation_guide;
}

void ToolchainDetector::detect_environment()
{
    m_info = ToolchainInfo{};
    m_detected = true;

#ifdef _WIN32
    // 1. Check GCC / MinGW (Scoop, MSYS2, MinGW)
    const char* user_profile = std::getenv("USERPROFILE");
    std::vector<std::filesystem::path> gcc_candidates;
    if (user_profile != nullptr)
    {
        gcc_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/gcc/current/bin/g++.exe");
        gcc_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/gcc/current/bin/gcc.exe");
    }
    gcc_candidates.push_back("C:/msys64/mingw64/bin/g++.exe");
    gcc_candidates.push_back("C:/msys64/ucrt64/bin/g++.exe");
    gcc_candidates.push_back("C:/MinGW/bin/g++.exe");

    for (const auto& candidate : gcc_candidates)
    {
        if (file_exists(candidate))
        {
            m_info.kind = ToolchainKind::MinGW_GCC;
            m_info.compiler_path = candidate;
            m_info.name = "MinGW-w64 GCC";

            // Find GCC C++ include directories (e.g. ../include/c++/<version>/iostream)
            const auto bin_dir = candidate.parent_path();
            const auto include_cxx = bin_dir.parent_path() / "include/c++";
            std::error_code ec;
            if (std::filesystem::exists(include_cxx, ec) && std::filesystem::is_directory(include_cxx, ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(include_cxx, ec))
                {
                    if (entry.is_directory() && file_exists(entry.path() / "iostream"))
                    {
                        m_info.has_standard_headers = true;
                        m_info.sdk_include_path = entry.path();
                        m_info.compiler_version = entry.path().filename().string();
                        m_info.name = "GCC " + m_info.compiler_version + " (MinGW)";
                        break;
                    }
                }
            }

            if (m_info.has_standard_headers)
            {
                m_info.status = ToolchainStatus::Ready;
                m_info.user_status_text = "Ready";
                return;
            }
            else
            {
                m_info.status = ToolchainStatus::MissingSdk;
                m_info.warning_message = "Found GCC compiler at " + candidate.string() + ", but Standard C++ headers (<iostream>) were not found.";
                m_info.installation_guide = "Please reinstall GCC or ensure libstdc++-dev headers are installed in the MinGW include directory.";
                return;
            }
        }
    }

    // 2. Check MSVC (Visual Studio / Build Tools)
    const std::filesystem::path msvc_roots[] = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC",
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/MSVC",
    };

    for (const auto& root : msvc_roots)
    {
        std::error_code ec;
        if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(root, ec))
            {
                const auto cl_path = entry.path() / "bin/Hostx64/x64/cl.exe";
                const auto include_path = entry.path() / "include";
                if (file_exists(cl_path) && file_exists(include_path / "iostream"))
                {
                    m_info.kind = ToolchainKind::MSVC;
                    m_info.compiler_path = cl_path;
                    m_info.sdk_include_path = include_path;
                    m_info.compiler_version = entry.path().filename().string();
                    m_info.name = "MSVC " + m_info.compiler_version;
                    m_info.has_standard_headers = true;
                    m_info.status = ToolchainStatus::Ready;
                    m_info.user_status_text = "Ready";
                    return;
                }
            }
        }
    }

    // 3. Check Clang / LLVM
    const std::filesystem::path llvm_candidates[] = {
        "ThirdParty/clangd/bin/clangd.exe",
        "C:/Program Files/LLVM/bin/clang++.exe",
        "C:/Program Files (x86)/LLVM/bin/clang++.exe",
    };
    for (const auto& candidate : llvm_candidates)
    {
        if (file_exists(candidate))
        {
            m_info.kind = ToolchainKind::Clang;
            m_info.compiler_path = candidate;
            m_info.name = "Clang / LLVM";
            m_info.status = ToolchainStatus::Ready;
            m_info.has_standard_headers = true;
            m_info.user_status_text = "Ready";
            return;
        }
    }

#else
    // Comprehensive macOS & Linux Compiler / SDK Discovery
    const std::filesystem::path unix_compilers[] = {
        // macOS Apple Silicon (M1/M2/M3/M4) Homebrew
        "/opt/homebrew/bin/clang++",
        "/opt/homebrew/opt/llvm/bin/clang++",
        "/opt/homebrew/opt/llvm@19/bin/clang++",
        "/opt/homebrew/opt/llvm@18/bin/clang++",
        "/opt/homebrew/opt/llvm@17/bin/clang++",
        "/opt/homebrew/bin/g++-14",
        "/opt/homebrew/bin/g++-13",
        // macOS Xcode & Command Line Tools
        "/Library/Developer/CommandLineTools/usr/bin/clang++",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++",
        // macOS Intel Homebrew & MacPorts
        "/usr/local/opt/llvm/bin/clang++",
        "/usr/local/bin/clang++",
        "/usr/local/bin/g++",
        "/opt/local/bin/clang++",
        // Linux Standard & Versioned Clang
        "/usr/bin/clang++",
        "/usr/bin/clang++-19",
        "/usr/bin/clang++-18",
        "/usr/bin/clang++-17",
        "/usr/bin/clang++-16",
        "/usr/bin/clang++-15",
        "/usr/lib/llvm-19/bin/clang++",
        "/usr/lib/llvm-18/bin/clang++",
        "/usr/lib/llvm-17/bin/clang++",
        // Linux Standard & Versioned GCC
        "/usr/bin/g++",
        "/usr/bin/g++-14",
        "/usr/bin/g++-13",
        "/usr/bin/g++-12",
        "/usr/bin/g++-11",
        "/usr/bin/c++",
    };
    for (const auto& p : unix_compilers)
    {
        if (file_exists(p))
        {
            const std::string name = p.filename().string();
            const bool is_clang = name.find("clang") != std::string::npos;
            m_info.kind = is_clang ? ToolchainKind::Clang : ToolchainKind::Gcc;
            m_info.compiler_path = p;
            m_info.name = is_clang ? ("Clang++ (" + name + ")") : ("GCC / G++ (" + name + ")");
            m_info.status = ToolchainStatus::Ready;
            m_info.has_standard_headers = true;
            m_info.user_status_text = "Ready";
            return;
        }
    }
#endif

    // Toolchain is NOT installed (Kosongan)
    m_info.status = ToolchainStatus::MissingCompiler;
    m_info.kind = ToolchainKind::None;
    m_info.name = "None (Not Configured)";
    m_info.has_standard_headers = false;
    m_info.warning_message = "No C++ Compiler or Platform SDK detected on this system.";
    m_info.installation_guide = 
        "To enable full C++ IntelliSense, code completion, and compilation (like CLion):\n"
        "1. Windows: Install GCC via Scoop (`scoop install gcc`) or Visual Studio Build Tools.\n"
        "2. macOS: Run `xcode-select --install` in terminal.\n"
        "3. Linux: Run `sudo apt install build-essential` (Ubuntu/Debian) or `sudo pacman -S base-devel` (Arch).";
}

} // namespace Zenvra::Language::Toolchain
