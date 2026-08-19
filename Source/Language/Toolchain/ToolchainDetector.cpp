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

#ifdef _WIN32
static std::vector<std::filesystem::path> discover_windows_sdk_includes()
{
    std::vector<std::filesystem::path> includes;
    const std::filesystem::path kits_roots[] = {
        "C:/Program Files (x86)/Windows Kits/10/Include",
        "C:/Program Files/Windows Kits/10/Include",
    };

    std::vector<std::filesystem::path> version_dirs;
    std::error_code ec;

    for (const auto& root : kits_roots)
    {
        if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(root, ec))
            {
                if (entry.is_directory(ec))
                {
                    const auto ucrt_path = entry.path() / "ucrt";
                    const auto um_path = entry.path() / "um";
                    if (std::filesystem::exists(ucrt_path, ec) || std::filesystem::exists(um_path, ec))
                    {
                        version_dirs.push_back(entry.path());
                    }
                }
            }
        }
    }

    // Sort descending so the highest SDK version (e.g. 10.0.26100.0 > 10.0.22621.0 > 10.0.19041.0) is selected
    std::sort(version_dirs.begin(), version_dirs.end(), [](const auto& a, const auto& b) {
        return a.filename().string() > b.filename().string();
    });

    if (!version_dirs.empty())
    {
        const auto& best_sdk = version_dirs.front();
        const std::string subdirs[] = { "ucrt", "shared", "um", "winrt", "cppwinrt" };
        for (const auto& sub : subdirs)
        {
            const auto p = best_sdk / sub;
            if (std::filesystem::exists(p, ec))
            {
                includes.push_back(p);
            }
        }
    }

    return includes;
}

static std::vector<std::filesystem::path> discover_msvc_includes(std::string* out_msvc_version = nullptr, std::filesystem::path* out_cl_path = nullptr)
{
    std::vector<std::filesystem::path> includes;
    const std::filesystem::path vs_base_dirs[] = {
        "C:/Program Files/Microsoft Visual Studio",
        "C:/Program Files (x86)/Microsoft Visual Studio",
    };

    std::vector<std::filesystem::path> msvc_dirs;
    std::error_code ec;

    for (const auto& base : vs_base_dirs)
    {
        if (std::filesystem::exists(base, ec) && std::filesystem::is_directory(base, ec))
        {
            for (const auto& year_entry : std::filesystem::directory_iterator(base, ec))
            {
                if (!year_entry.is_directory(ec)) continue;
                for (const auto& edition_entry : std::filesystem::directory_iterator(year_entry.path(), ec))
                {
                    if (!edition_entry.is_directory(ec)) continue;
                    const auto msvc_root = edition_entry.path() / "VC/Tools/MSVC";
                    if (std::filesystem::exists(msvc_root, ec) && std::filesystem::is_directory(msvc_root, ec))
                    {
                        for (const auto& ver_entry : std::filesystem::directory_iterator(msvc_root, ec))
                        {
                            if (ver_entry.is_directory(ec))
                            {
                                const auto inc = ver_entry.path() / "include";
                                if (std::filesystem::exists(inc / "iostream", ec))
                                {
                                    msvc_dirs.push_back(ver_entry.path());
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::sort(msvc_dirs.begin(), msvc_dirs.end(), [](const auto& a, const auto& b) {
        return a.filename().string() > b.filename().string();
    });

    if (!msvc_dirs.empty())
    {
        const auto& best_msvc = msvc_dirs.front();
        const auto inc = best_msvc / "include";
        const auto atlmfc = best_msvc / "atlmfc/include";
        if (std::filesystem::exists(inc, ec)) includes.push_back(inc);
        if (std::filesystem::exists(atlmfc, ec)) includes.push_back(atlmfc);

        if (out_msvc_version != nullptr) {
            *out_msvc_version = best_msvc.filename().string();
        }
        if (out_cl_path != nullptr) {
            const auto cl_x64 = best_msvc / "bin/Hostx64/x64/cl.exe";
            if (std::filesystem::exists(cl_x64, ec)) {
                *out_cl_path = cl_x64;
            }
        }
    }

    return includes;
}

static std::vector<std::filesystem::path> discover_clang_resource_includes(const std::string& user_profile)
{
    std::vector<std::filesystem::path> includes;
    std::vector<std::filesystem::path> search_roots = {
        "C:/Program Files/LLVM/lib/clang",
        "C:/Program Files (x86)/LLVM/lib/clang",
    };
    if (!user_profile.empty())
    {
        search_roots.push_back(std::filesystem::path(user_profile) / "scoop/apps/llvm/current/lib/clang");
    }

    std::error_code ec;
    std::vector<std::filesystem::path> clang_ver_dirs;
    for (const auto& root : search_roots)
    {
        if (std::filesystem::exists(root, ec) && std::filesystem::is_directory(root, ec))
        {
            for (const auto& entry : std::filesystem::directory_iterator(root, ec))
            {
                if (entry.is_directory(ec))
                {
                    const auto inc = entry.path() / "include";
                    if (std::filesystem::exists(inc, ec))
                    {
                        clang_ver_dirs.push_back(inc);
                    }
                }
            }
        }
    }

    std::sort(clang_ver_dirs.begin(), clang_ver_dirs.end(), [](const auto& a, const auto& b) {
        return a.string() > b.string();
    });

    if (!clang_ver_dirs.empty())
    {
        includes.push_back(clang_ver_dirs.front());
    }

    return includes;
}
#endif

void ToolchainDetector::detect_environment()
{
    m_info = ToolchainInfo{};
    m_detected = true;

#ifdef _WIN32
    std::string user_profile;
    char user_profile_buf[MAX_PATH] = {};
    std::size_t user_profile_len = 0;
    if (getenv_s(&user_profile_len, user_profile_buf, sizeof(user_profile_buf), "USERPROFILE") == 0 && user_profile_len > 0)
    {
        user_profile = user_profile_buf;
    }

    // Discover all system includes dynamically across Windows SDK versions and MSVC editions
    const auto sdk_includes = discover_windows_sdk_includes();
    std::string msvc_version;
    std::filesystem::path msvc_cl_path;
    const auto msvc_includes = discover_msvc_includes(&msvc_version, &msvc_cl_path);
    const auto clang_res_includes = discover_clang_resource_includes(user_profile);

    // Combine all standard include paths
    std::vector<std::filesystem::path> all_system_includes;
    for (const auto& p : msvc_includes) all_system_includes.push_back(p);
    for (const auto& p : sdk_includes) all_system_includes.push_back(p);
    for (const auto& p : clang_res_includes) all_system_includes.push_back(p);

    m_info.system_include_paths = all_system_includes;

    // 1. Check Clang / LLVM (Scoop, Standard, or ThirdParty)
    std::vector<std::filesystem::path> llvm_candidates = {
        "C:/Program Files/LLVM/bin/clang++.exe",
        "C:/Program Files/LLVM/bin/clang-cl.exe",
        "C:/Program Files (x86)/LLVM/bin/clang++.exe",
    };
    if (!user_profile.empty())
    {
        llvm_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/llvm/current/bin/clang-cl.exe");
        llvm_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/llvm/current/bin/clang++.exe");
    }
    for (const auto& candidate : llvm_candidates)
    {
        if (file_exists(candidate))
        {
            m_info.kind = ToolchainKind::Clang;
            m_info.compiler_path = candidate;
            m_info.name = "Clang / LLVM";
            m_info.status = ToolchainStatus::Ready;
            m_info.has_standard_headers = !msvc_includes.empty() || !sdk_includes.empty();
            if (!msvc_includes.empty()) m_info.sdk_include_path = msvc_includes.front();
            m_info.user_status_text = "Ready";
            return;
        }
    }

    // 2. Check MSVC (Visual Studio 2026/18, 2022, 2019, BuildTools, Preview)
    if (!msvc_cl_path.empty() && !msvc_includes.empty())
    {
        m_info.kind = ToolchainKind::MSVC;
        m_info.compiler_path = msvc_cl_path;
        m_info.sdk_include_path = msvc_includes.front();
        m_info.compiler_version = msvc_version;
        m_info.name = "MSVC " + msvc_version;
        m_info.has_standard_headers = true;
        m_info.status = ToolchainStatus::Ready;
        m_info.user_status_text = "Ready";
        return;
    }

    // 3. Check GCC / MinGW (Scoop, MSYS2, MinGW)
    std::vector<std::filesystem::path> gcc_candidates;
    if (!user_profile.empty())
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
