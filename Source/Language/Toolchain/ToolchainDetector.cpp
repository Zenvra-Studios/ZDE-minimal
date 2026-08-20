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

static std::vector<std::filesystem::path> discover_gcc_includes(const std::string& user_profile, std::string* out_gcc_version = nullptr, std::filesystem::path* out_gxx_path = nullptr)
{
    std::vector<std::filesystem::path> includes;
    std::vector<std::filesystem::path> gcc_candidates;
    if (!user_profile.empty())
    {
        gcc_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/gcc/current/bin/g++.exe");
        gcc_candidates.push_back(std::filesystem::path(user_profile) / "scoop/apps/gcc/current/bin/gcc.exe");
    }
    gcc_candidates.push_back("C:/msys64/mingw64/bin/g++.exe");
    gcc_candidates.push_back("C:/msys64/ucrt64/bin/g++.exe");
    gcc_candidates.push_back("C:/msys64/clang64/bin/g++.exe");
    gcc_candidates.push_back("C:/MinGW/bin/g++.exe");

    std::error_code ec;
    for (const auto& candidate : gcc_candidates)
    {
        if (file_exists(candidate))
        {
            if (out_gxx_path != nullptr && out_gxx_path->empty())
            {
                *out_gxx_path = candidate;
            }
            const auto bin_dir = candidate.parent_path();
            const auto prefix = bin_dir.parent_path();
            const auto include_cxx = prefix / "include/c++";

            if (std::filesystem::exists(include_cxx, ec) && std::filesystem::is_directory(include_cxx, ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(include_cxx, ec))
                {
                    if (entry.is_directory() && file_exists(entry.path() / "iostream"))
                    {
                        const auto ver_dir = entry.path();
                        if (out_gcc_version != nullptr && out_gcc_version->empty())
                        {
                            *out_gcc_version = ver_dir.filename().string();
                        }
                        includes.push_back(ver_dir);

                        for (const auto& sub : std::filesystem::directory_iterator(ver_dir, ec))
                        {
                            if (sub.is_directory() && (sub.path().filename().string().find("mingw") != std::string::npos || sub.path().filename().string().find("x86_64") != std::string::npos || sub.path().filename() == "backward"))
                            {
                                includes.push_back(sub.path());
                            }
                        }

                        for (const auto& sub : std::filesystem::directory_iterator(prefix, ec))
                        {
                            if (sub.is_directory() && sub.path().filename().string().find("mingw") != std::string::npos)
                            {
                                const auto target_inc = sub.path() / "include";
                                if (std::filesystem::exists(target_inc, ec))
                                {
                                    includes.push_back(target_inc);
                                }
                            }
                        }
                        const auto direct_inc = prefix / "include";
                        if (std::filesystem::exists(direct_inc, ec))
                        {
                            includes.push_back(direct_inc);
                        }

                        return includes;
                    }
                }
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

#ifndef _WIN32
static std::vector<std::filesystem::path> discover_unix_system_includes(std::string* out_compiler_version = nullptr)
{
    std::vector<std::filesystem::path> includes;
    std::error_code ec;

    // 1. GCC C++ standard header versions (e.g. /usr/include/c++/14, 13, 12, etc.)
    const std::filesystem::path cxx_roots[] = {
        "/usr/include/c++",
        "/usr/local/include/c++",
        "/opt/homebrew/opt/llvm/include/c++/v1",
        "/opt/homebrew/opt/llvm@19/include/c++/v1",
        "/opt/homebrew/opt/llvm@18/include/c++/v1",
        "/opt/homebrew/opt/llvm@17/include/c++/v1",
        "/usr/local/opt/llvm/include/c++/v1",
        "/Library/Developer/CommandLineTools/usr/include/c++/v1",
        "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1",
    };

    std::vector<std::filesystem::path> version_dirs;
    for (const auto& cxx_root : cxx_roots)
    {
        if (std::filesystem::exists(cxx_root, ec) && std::filesystem::is_directory(cxx_root, ec))
        {
            if (file_exists(cxx_root / "iostream") || file_exists(cxx_root / "vector"))
            {
                includes.push_back(cxx_root);
            }
            for (const auto& entry : std::filesystem::directory_iterator(cxx_root, std::filesystem::directory_options::skip_permission_denied, ec))
            {
                if (entry.is_directory(ec) && (file_exists(entry.path() / "iostream") || file_exists(entry.path() / "vector")))
                {
                    version_dirs.push_back(entry.path());
                }
            }
        }
    }

    std::sort(version_dirs.begin(), version_dirs.end(), [](const auto& a, const auto& b) {
        return a.filename().string() > b.filename().string();
    });

    if (!version_dirs.empty())
    {
        const auto& best_ver_dir = version_dirs.front();
        if (out_compiler_version != nullptr && out_compiler_version->empty())
        {
            *out_compiler_version = best_ver_dir.filename().string();
        }
        includes.push_back(best_ver_dir);
        const auto backward = best_ver_dir / "backward";
        if (std::filesystem::exists(backward, ec))
        {
            includes.push_back(backward);
        }

        for (const auto& sub : std::filesystem::directory_iterator(best_ver_dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (sub.is_directory(ec))
            {
                const std::string sub_name = sub.path().filename().string();
                if (sub_name.find("linux") != std::string::npos ||
                    sub_name.find("gnu") != std::string::npos ||
                    sub_name.find("x86_64") != std::string::npos ||
                    sub_name.find("aarch64") != std::string::npos ||
                    sub_name.find("arm") != std::string::npos ||
                    sub_name.find("darwin") != std::string::npos)
                {
                    includes.push_back(sub.path());
                }
            }
        }
    }

    // 2. Linux Multiarch architecture-specific headers
    const std::filesystem::path multiarch_candidates[] = {
        "/usr/include/x86_64-linux-gnu",
        "/usr/include/aarch64-linux-gnu",
        "/usr/include/arm-linux-gnueabihf",
        "/usr/include/i386-linux-gnu",
        "/usr/include/riscv64-linux-gnu",
        "/usr/local/include",
        "/usr/include",
        "/opt/homebrew/include",
        "/usr/local/opt/llvm/include",
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include",
    };

    for (const auto& cand : multiarch_candidates)
    {
        if (std::filesystem::exists(cand, ec) && std::filesystem::is_directory(cand, ec))
        {
            includes.push_back(cand);
            // Check for nested c++ version folder under multiarch root
            const auto multi_cxx = cand / "c++";
            if (std::filesystem::exists(multi_cxx, ec) && std::filesystem::is_directory(multi_cxx, ec))
            {
                for (const auto& entry : std::filesystem::directory_iterator(multi_cxx, std::filesystem::directory_options::skip_permission_denied, ec))
                {
                    if (entry.is_directory(ec))
                    {
                        includes.push_back(entry.path());
                    }
                }
            }
        }
    }

    // 3. GCC compiler internal include headers (/usr/lib/gcc/<arch>/<ver>/include)
    const std::filesystem::path gcc_lib_root = "/usr/lib/gcc";
    if (std::filesystem::exists(gcc_lib_root, ec) && std::filesystem::is_directory(gcc_lib_root, ec))
    {
        for (const auto& arch_entry : std::filesystem::directory_iterator(gcc_lib_root, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (arch_entry.is_directory(ec))
            {
                for (const auto& ver_entry : std::filesystem::directory_iterator(arch_entry.path(), std::filesystem::directory_options::skip_permission_denied, ec))
                {
                    if (ver_entry.is_directory(ec))
                    {
                        const auto inc = ver_entry.path() / "include";
                        const auto inc_fixed = ver_entry.path() / "include-fixed";
                        if (std::filesystem::exists(inc, ec)) includes.push_back(inc);
                        if (std::filesystem::exists(inc_fixed, ec)) includes.push_back(inc_fixed);
                    }
                }
            }
        }
    }

    // 4. Clang compiler resource headers (/usr/lib/llvm-*/lib/clang/*/include or /usr/lib/clang/*/include)
    const std::filesystem::path clang_lib_roots[] = {
        "/usr/lib/llvm-19/lib/clang",
        "/usr/lib/llvm-18/lib/clang",
        "/usr/lib/llvm-17/lib/clang",
        "/usr/lib/llvm-16/lib/clang",
        "/usr/lib/llvm-15/lib/clang",
        "/usr/lib/clang",
        "/opt/homebrew/opt/llvm/lib/clang",
        "/usr/local/opt/llvm/lib/clang",
    };

    for (const auto& cl_root : clang_lib_roots)
    {
        if (std::filesystem::exists(cl_root, ec) && std::filesystem::is_directory(cl_root, ec))
        {
            for (const auto& ver_entry : std::filesystem::directory_iterator(cl_root, std::filesystem::directory_options::skip_permission_denied, ec))
            {
                if (ver_entry.is_directory(ec))
                {
                    const auto inc = ver_entry.path() / "include";
                    if (std::filesystem::exists(inc, ec))
                    {
                        includes.push_back(inc);
                    }
                }
            }
        }
    }

    // Remove duplicates while preserving order
    std::vector<std::filesystem::path> unique_includes;
    for (const auto& p : includes)
    {
        bool dup = false;
        for (const auto& u : unique_includes)
        {
            if (std::filesystem::equivalent(p, u, ec))
            {
                dup = true;
                break;
            }
        }
        if (!dup)
        {
            unique_includes.push_back(p);
        }
    }

    return unique_includes;
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

    // Discover all system includes dynamically across Windows SDK versions, MSVC editions, Clang, and GCC/MinGW
    const auto sdk_includes = discover_windows_sdk_includes();
    std::string msvc_version;
    std::filesystem::path msvc_cl_path;
    const auto msvc_includes = discover_msvc_includes(&msvc_version, &msvc_cl_path);
    const auto clang_res_includes = discover_clang_resource_includes(user_profile);
    std::string gcc_version;
    std::filesystem::path gcc_gxx_path;
    const auto gcc_includes = discover_gcc_includes(user_profile, &gcc_version, &gcc_gxx_path);

    // Combine all standard include paths
    std::vector<std::filesystem::path> all_system_includes;
    for (const auto& p : msvc_includes) all_system_includes.push_back(p);
    for (const auto& p : sdk_includes) all_system_includes.push_back(p);
    for (const auto& p : clang_res_includes) all_system_includes.push_back(p);
    for (const auto& p : gcc_includes) all_system_includes.push_back(p);

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
            m_info.has_standard_headers = !msvc_includes.empty() || !sdk_includes.empty() || !gcc_includes.empty();
            if (!msvc_includes.empty()) m_info.sdk_include_path = msvc_includes.front();
            else if (!gcc_includes.empty()) m_info.sdk_include_path = gcc_includes.front();
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
    if (!gcc_gxx_path.empty() && !gcc_includes.empty())
    {
        m_info.kind = ToolchainKind::MinGW_GCC;
        m_info.compiler_path = gcc_gxx_path;
        m_info.sdk_include_path = gcc_includes.front();
        m_info.compiler_version = gcc_version;
        m_info.name = "GCC " + gcc_version + " (MinGW)";
        m_info.has_standard_headers = true;
        m_info.status = ToolchainStatus::Ready;
        m_info.user_status_text = "Ready";
        return;
    }
#else
    // Comprehensive macOS & Linux Compiler / SDK Discovery
    std::string detected_version;
    const auto unix_includes = discover_unix_system_includes(&detected_version);
    m_info.system_include_paths = unix_includes;
    if (!unix_includes.empty())
    {
        m_info.sdk_include_path = unix_includes.front();
        m_info.has_standard_headers = true;
    }

    const std::filesystem::path unix_compilers[] = {
        // Linux Standard & Versioned GCC (primary on Linux)
        "/usr/bin/g++",
        "/usr/bin/g++-14",
        "/usr/bin/g++-13",
        "/usr/bin/g++-12",
        "/usr/bin/g++-11",
        "/usr/bin/c++",
        "/usr/bin/gcc",
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
    };
    for (const auto& p : unix_compilers)
    {
        if (file_exists(p))
        {
            const std::string name = p.filename().string();
            const bool is_clang = name.find("clang") != std::string::npos;
            m_info.kind = is_clang ? ToolchainKind::Clang : ToolchainKind::Gcc;
            m_info.compiler_path = p;
            m_info.compiler_version = detected_version;
            m_info.name = is_clang ? ("Clang++ (" + name + ")") : ("GCC / G++ (" + name + ")");
            m_info.status = ToolchainStatus::Ready;
            m_info.has_standard_headers = !m_info.system_include_paths.empty();
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
