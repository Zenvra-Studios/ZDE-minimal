#include "Language/Registry/ServerRegistry.h"

#include <array>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/param.h>
#endif

namespace Zenvra::Language::Registry
{

ServerRegistry& ServerRegistry::instance() noexcept
{
    static ServerRegistry registry;
    return registry;
}

ServerRegistry::ServerRegistry()
{
    initialize_default_profiles();
}

void ServerRegistry::register_profile(ServerProfile profile)
{
    for (const auto& ext : profile.extensions)
    {
        m_language_by_extension[ext] = profile.language_id;
    }
    m_profiles_by_language[profile.language_id] = std::move(profile);
}

const ServerProfile* ServerRegistry::find_profile_for_filename(std::string_view filename) const noexcept
{
    if (filename.empty()) return nullptr;

    const std::filesystem::path p(filename);
    const std::string base_name = p.filename().string();
    std::string base_lower = base_name;
    for (char& c : base_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const std::string ext = p.extension().string();
    std::string ext_lower = ext;
    for (char& c : ext_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // 1. Check exact base filename / lowercase filename (e.g. CMakeLists.txt, meson.build)
    auto it = m_language_by_extension.find(base_name);
    if (it != m_language_by_extension.end()) return find_profile_for_language(it->second);

    it = m_language_by_extension.find(base_lower);
    if (it != m_language_by_extension.end()) return find_profile_for_language(it->second);

    // 2. Check extension / lowercase extension (e.g. .cmake, .cpp, .py)
    if (!ext.empty())
    {
        it = m_language_by_extension.find(ext);
        if (it != m_language_by_extension.end()) return find_profile_for_language(it->second);

        it = m_language_by_extension.find(ext_lower);
        if (it != m_language_by_extension.end()) return find_profile_for_language(it->second);
    }

    // 3. Fallback checks for well-known build files
    if (base_lower == "cmakelists.txt" || base_lower.ends_with(".cmake"))
    {
        return find_profile_for_language("cmake");
    }
    if (base_lower == "meson.build" || base_lower == "meson_options.txt")
    {
        return find_profile_for_language("meson");
    }

    return nullptr;
}

const ServerProfile* ServerRegistry::find_profile_for_extension(std::string_view extension) const noexcept
{
    const std::string ext_str(extension);
    auto it = m_language_by_extension.find(ext_str);
    if (it != m_language_by_extension.end())
    {
        return find_profile_for_language(it->second);
    }

    std::string ext_lower = ext_str;
    for (char& c : ext_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    it = m_language_by_extension.find(ext_lower);
    if (it != m_language_by_extension.end())
    {
        return find_profile_for_language(it->second);
    }

    return nullptr;
}

const ServerProfile* ServerRegistry::find_profile_for_language(std::string_view language_id) const noexcept
{
    const std::string lang_str(language_id);
    const auto it = m_profiles_by_language.find(lang_str);
    if (it != m_profiles_by_language.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::vector<const ServerProfile*> ServerRegistry::get_all_profiles() const
{
    std::vector<const ServerProfile*> result;
    result.reserve(m_profiles_by_language.size());
    for (const auto& [id, profile] : m_profiles_by_language)
    {
        result.push_back(&profile);
    }
    return result;
}

std::filesystem::path ServerRegistry::find_executable_in_system(std::string_view executable_name) const
{
    const std::string exe_str(executable_name);
    std::vector<std::string> candidate_names = { exe_str };

    if (exe_str == "cmake-language-server" || exe_str == "cmake-ls" || exe_str == "cmakels" || exe_str == "cmakels-win64")
    {
        candidate_names = { "cmakels", "cmake-language-server", "cmake-ls", "cmakels-win64", "neocmakelsp" };
    }
    else if (exe_str == "clangd")
    {
        candidate_names = { "clangd", "clangd-19", "clangd-18", "clangd-17", "clangd-16", "clangd-15" };
    }
    else if (exe_str == "pyright-langserver" || exe_str == "pyright" || exe_str == "pylsp")
    {
        candidate_names = { "pyright-langserver", "pyright", "pylsp", "jedi-language-server" };
    }
    else if (exe_str == "typescript-language-server" || exe_str == "tls" || exe_str == "vtsls" || exe_str == "tsserver")
    {
        candidate_names = { "typescript-language-server", "tls", "vtsls", "deno", "bun", "tsserver" };
    }
    else if (exe_str == "rust-analyzer")
    {
        candidate_names = { "rust-analyzer", "rust-analyzer-linux", "rust-analyzer-mac" };
    }
    else if (exe_str == "vscode-html-language-server" || exe_str == "html-languageserver" || exe_str == "html")
    {
        candidate_names = { "vscode-html-language-server", "html-languageserver", "html" };
    }
    else if (exe_str == "vscode-css-language-server")
    {
        candidate_names = { "vscode-css-language-server", "css-languageserver" };
    }
    else if (exe_str == "vscode-json-language-server")
    {
        candidate_names = { "vscode-json-language-server", "json-languageserver" };
    }
    else if (exe_str == "csharp-ls" || exe_str == "omnisharp")
    {
        candidate_names = { "csharp-ls", "omnisharp" };
    }

    for (const auto& cur_name : candidate_names)
    {
        std::string exe_with_ext = cur_name;
#if defined(_WIN32)
        if (!exe_with_ext.ends_with(".exe"))
        {
            exe_with_ext += ".exe";
        }
#endif

        // 0. Absolute Top Priority: Check alongside the running executable directory & TLS plugin folders
#if defined(_WIN32)
        std::array<wchar_t, 32768> exe_buffer{};
        const DWORD exe_len = GetModuleFileNameW(nullptr, exe_buffer.data(), static_cast<DWORD>(exe_buffer.size()));
        if (exe_len > 0 && exe_len < exe_buffer.size())
        {
            const std::filesystem::path app_dir = std::filesystem::path(exe_buffer.data()).parent_path();
            const std::filesystem::path app_candidates[] = {
                app_dir / "plugins" / "lsp" / "tls" / exe_with_ext,
                app_dir / "plugins" / "lsp" / "html" / exe_with_ext,
                app_dir / "plugins" / "lsp" / "typescript-language-server" / exe_with_ext,
                app_dir / "plugins" / "lsp" / "vscode-html-language-server" / exe_with_ext,
                app_dir / "plugins" / "lsp" / exe_with_ext,
                app_dir / "plugins" / "html" / exe_with_ext,
                app_dir / "plugins" / "tls" / exe_with_ext,
                app_dir / "plugins" / exe_with_ext,
                app_dir / "lsp" / "html" / exe_with_ext,
                app_dir / "lsp" / "tls" / exe_with_ext,
                app_dir / "lsp" / exe_with_ext,
                app_dir / "html" / exe_with_ext,
                app_dir / "tls" / exe_with_ext,
                app_dir / "bin" / exe_with_ext,
                app_dir / exe_with_ext,
            };

            for (const auto& candidate : app_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }

            // Check relative ThirdParty up to 5 levels above executable
            std::filesystem::path check_dir = app_dir;
            for (int i = 0; i < 5; ++i)
            {
                const std::filesystem::path tp = check_dir / "ThirdParty";
                std::error_code ec;
                if (std::filesystem::exists(tp, ec) && std::filesystem::is_directory(tp, ec))
                {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(tp, ec))
                    {
                        if (entry.is_regular_file() && entry.path().filename() == exe_with_ext)
                        {
                            return entry.path();
                        }
                    }
                }
                if (!check_dir.has_parent_path() || check_dir == check_dir.parent_path()) break;
                check_dir = check_dir.parent_path();
            }
        }
#elif defined(__APPLE__)
        char apple_exe_path[PATH_MAX]{};
        uint32_t apple_exe_size = sizeof(apple_exe_path);
        if (_NSGetExecutablePath(apple_exe_path, &apple_exe_size) == 0)
        {
            std::error_code ec;
            const std::filesystem::path app_dir = std::filesystem::canonical(apple_exe_path, ec).parent_path();
            const std::filesystem::path app_candidates[] = {
                app_dir / "plugins" / "lsp" / cur_name,
                app_dir / "plugins" / cur_name,
                app_dir / "../Resources/plugins/lsp" / cur_name,
                app_dir / "../Resources" / cur_name,
                app_dir / cur_name,
            };
            for (const auto& candidate : app_candidates)
            {
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }
        }
#elif defined(__linux__) || defined(__unix__)
        std::error_code ec_proc;
        if (std::filesystem::exists("/proc/self/exe", ec_proc))
        {
            const std::filesystem::path app_dir = std::filesystem::canonical("/proc/self/exe", ec_proc).parent_path();
            const std::filesystem::path app_candidates[] = {
                app_dir / "plugins" / "lsp" / cur_name,
                app_dir / "plugins" / cur_name,
                app_dir / "bin" / cur_name,
                app_dir / cur_name,
            };
            for (const auto& candidate : app_candidates)
            {
                if (std::filesystem::exists(candidate, ec_proc) && std::filesystem::is_regular_file(candidate, ec_proc))
                {
                    return candidate;
                }
            }
        }
#endif

        // 0b. Traverse upward from current directory and check candidate folders (plugins, ThirdParty, etc.)
        std::vector<std::filesystem::path> search_bases;
        {
            std::error_code ec;
            std::filesystem::path cur = std::filesystem::current_path(ec);
            for (int i = 0; i < 8 && !cur.empty(); ++i)
            {
                search_bases.push_back(cur);
                const auto parent = cur.parent_path();
                if (parent == cur) break;
                cur = parent;
            }
        }

        // Direct local paths in search bases
        for (const auto& base : search_bases)
        {
            const std::filesystem::path direct_candidates[] = {
                base / "plugins" / "lsp" / exe_with_ext,
                base / "plugins" / exe_with_ext,
                base / exe_with_ext,
                base / "ThirdParty" / exe_with_ext,
                base / "ThirdParty" / "bin" / exe_with_ext,
            };

            for (const auto& candidate : direct_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }

            // Check ThirdParty and plugins subdirectories
            const std::filesystem::path container_dirs[] = {
                base / "ThirdParty",
                base / "plugins" / "lsp",
                base / "plugins",
            };

            for (const auto& container : container_dirs)
            {
                std::error_code ec;
                if (std::filesystem::exists(container, ec) && std::filesystem::is_directory(container, ec))
                {
                    for (const auto& entry : std::filesystem::directory_iterator(container, ec))
                    {
                        if (entry.is_directory())
                        {
                            const auto sub_candidate = entry.path() / exe_with_ext;
                            if (std::filesystem::exists(sub_candidate, ec) && std::filesystem::is_regular_file(sub_candidate, ec))
                            {
                                return sub_candidate;
                            }
                            const auto sub_bin = entry.path() / "bin" / exe_with_ext;
                            if (std::filesystem::exists(sub_bin, ec) && std::filesystem::is_regular_file(sub_bin, ec))
                            {
                                return sub_bin;
                            }
                        }
                    }
                }
            }
        }

#if defined(_WIN32)
        // 2. Second Priority (Windows): Check Scoop, WinGet, Chocolatey, NuGet, and LocalAppData environment paths
        const char* user_profile = std::getenv("USERPROFILE");
        const char* local_appdata = std::getenv("LOCALAPPDATA");
        const char* appdata = std::getenv("APPDATA");
        const char* program_data = std::getenv("ProgramData");

        if (user_profile != nullptr)
        {
            const std::filesystem::path up(user_profile);
            const std::filesystem::path user_candidates[] = {
                // Scoop LLVM / clangd / cmake-ls
                up / "scoop" / "apps" / "llvm" / "current" / "bin" / exe_with_ext,
                up / "scoop" / "apps" / cur_name / "current" / "bin" / exe_with_ext,
                up / "scoop" / "shims" / exe_with_ext,
                // WinGet Links / Packages
                up / "AppData" / "Local" / "Microsoft" / "WinGet" / "Links" / exe_with_ext,
                // NuGet package fallbacks
                up / ".nuget" / "packages" / cur_name / exe_with_ext,
                // Cargo / Rust bin
                up / ".cargo" / "bin" / exe_with_ext,
                // Go bin
                up / "go" / "bin" / exe_with_ext,
            };

            for (const auto& candidate : user_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }
        }

        if (local_appdata != nullptr)
        {
            const std::filesystem::path lad(local_appdata);
            const std::filesystem::path lad_candidates[] = {
                lad / "Microsoft" / "WinGet" / "Links" / exe_with_ext,
                lad / "pnpm" / exe_with_ext,
                lad / "Volta" / "bin" / exe_with_ext,
                lad / "Programs" / "Python" / "Python313" / "Scripts" / exe_with_ext,
                lad / "Programs" / "Python" / "Python312" / "Scripts" / exe_with_ext,
                lad / "Programs" / "Python" / "Python311" / "Scripts" / exe_with_ext,
                lad / "Programs" / "LLVM" / "bin" / exe_with_ext,
            };
            for (const auto& candidate : lad_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }
        }

        if (program_data != nullptr)
        {
            const std::filesystem::path pd(program_data);
            const std::filesystem::path choco_candidates[] = {
                pd / "chocolatey" / "bin" / exe_with_ext,
                pd / "chocolatey" / "lib" / "llvm" / "tools" / "llvm" / "bin" / exe_with_ext,
                pd / "chocolatey" / "lib" / cur_name / "tools" / exe_with_ext,
            };
            for (const auto& candidate : choco_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }
        }

        if (appdata != nullptr)
        {
            const std::filesystem::path ad(appdata);
            const std::filesystem::path npm_candidate = ad / "npm" / exe_with_ext;
            std::error_code ec;
            if (std::filesystem::exists(npm_candidate, ec) && std::filesystem::is_regular_file(npm_candidate, ec))
            {
                return npm_candidate;
            }
        }

        // 3. Third Priority: SearchPathW (System Windows PATH search)
        std::wstring exe_w(exe_with_ext.begin(), exe_with_ext.end());
        std::array<wchar_t, 32768> resolved{};
        const DWORD length = SearchPathW(
            nullptr, exe_w.c_str(), nullptr, static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
        if (length > 0 && length < resolved.size())
        {
            return std::filesystem::path(resolved.data());
        }

        // 4. Fourth Priority: Program Files, MSYS2/MinGW, & Visual Studio standard installer paths
        const std::filesystem::path program_files_candidates[] = {
            std::filesystem::path("C:/Program Files/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/Go/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/mingw64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/ucrt64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/clang64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/usr/bin") / exe_with_ext,
            std::filesystem::path("C:/MinGW/bin") / exe_with_ext,
            // Visual Studio 2022 Bundled Clang/LLVM
            std::filesystem::path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/Llvm/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/Llvm/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/Microsoft Visual Studio/2022/Preview/VC/Tools/Llvm/bin") / exe_with_ext,
            // Visual Studio 2019 Bundled Clang/LLVM
            std::filesystem::path("C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/Llvm/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Tools/Llvm/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional/VC/Tools/Llvm/bin") / exe_with_ext,
        };
        for (const auto& candidate : program_files_candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                return candidate;
            }
        }
#else
        // 2. Second Priority (macOS & Linux): User Home Directory Toolchains
        const char* home_env = std::getenv("HOME");
        if (home_env != nullptr)
        {
            const std::filesystem::path home(home_env);
            const std::filesystem::path unix_user_candidates[] = {
                // User local bin & LLVM versioned binaries
                home / ".local" / "bin" / cur_name,
                home / ".local" / "bin" / (cur_name + "-19"),
                home / ".local" / "bin" / (cur_name + "-18"),
                home / ".local" / "bin" / (cur_name + "-17"),
                // Cargo / Rust bin
                home / ".cargo" / "bin" / cur_name,
                // Go bin
                home / "go" / "bin" / cur_name,
                // Flatpak user exports
                home / ".local" / "share" / "flatpak" / "exports" / "bin" / cur_name,
                // Homebrew user bin (Linuxbrew)
                home / ".linuxbrew" / "bin" / cur_name,
            };

            for (const auto& candidate : unix_user_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return candidate;
                }
            }
        }

        // 3. Third Priority (macOS & Linux): System / Package Manager Standard Paths
        const std::filesystem::path unix_system_candidates[] = {
            // Homebrew macOS (Apple Silicon M1/M2/M3/M4)
            std::filesystem::path("/opt/homebrew/bin") / cur_name,
            std::filesystem::path("/opt/homebrew/opt/llvm/bin") / cur_name,
            std::filesystem::path("/opt/homebrew/opt/llvm@19/bin") / cur_name,
            std::filesystem::path("/opt/homebrew/opt/llvm@18/bin") / cur_name,
            std::filesystem::path("/opt/homebrew/opt/llvm@17/bin") / cur_name,
            std::filesystem::path("/opt/homebrew/opt/cmake/bin") / cur_name,
            // Homebrew macOS (Intel x86_64) & Standard Local
            std::filesystem::path("/usr/local/bin") / cur_name,
            std::filesystem::path("/usr/local/opt/llvm/bin") / cur_name,
            // MacPorts (macOS)
            std::filesystem::path("/opt/local/bin") / cur_name,
            std::filesystem::path("/opt/local/libexec/llvm-19/bin") / cur_name,
            std::filesystem::path("/opt/local/libexec/llvm-18/bin") / cur_name,
            std::filesystem::path("/opt/local/libexec/llvm-17/bin") / cur_name,
            // Xcode / Command Line Tools (macOS)
            std::filesystem::path("/Library/Developer/CommandLineTools/usr/bin") / cur_name,
            std::filesystem::path("/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin") / cur_name,
            // Linux Distro Standard & Versioned LLVM (Ubuntu, Debian, Fedora, Arch, Alpine)
            std::filesystem::path("/usr/bin") / cur_name,
            std::filesystem::path("/usr/bin") / (cur_name + "-19"),
            std::filesystem::path("/usr/bin") / (cur_name + "-18"),
            std::filesystem::path("/usr/bin") / (cur_name + "-17"),
            std::filesystem::path("/usr/bin") / (cur_name + "-16"),
            std::filesystem::path("/usr/bin") / (cur_name + "-15"),
            std::filesystem::path("/usr/lib/llvm-19/bin") / cur_name,
            std::filesystem::path("/usr/lib/llvm-18/bin") / cur_name,
            std::filesystem::path("/usr/lib/llvm-17/bin") / cur_name,
            std::filesystem::path("/usr/lib/llvm-16/bin") / cur_name,
            std::filesystem::path("/usr/lib/llvm/bin") / cur_name,
            // Snap & Flatpak (Linux)
            std::filesystem::path("/snap/bin") / cur_name,
            std::filesystem::path("/var/lib/flatpak/exports/bin") / cur_name,
            std::filesystem::path("/bin") / cur_name,
            std::filesystem::path("/sbin") / cur_name,
            std::filesystem::path("/usr/sbin") / cur_name,
        };

        for (const auto& candidate : unix_system_candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                return candidate;
            }
        }

        // 4. Fourth Priority: PATH Environment Variable Tokenizer
        const char* path_env = std::getenv("PATH");
        if (path_env != nullptr)
        {
            std::stringstream ss(path_env);
            std::string dir;
            while (std::getline(ss, dir, ':'))
            {
                if (dir.empty()) continue;
                std::filesystem::path p = std::filesystem::path(dir) / cur_name;
                std::error_code ec;
                if (std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec))
                {
                    return p;
                }
            }
        }
#endif
    }

    return {};
}

void ServerRegistry::initialize_default_profiles()
{
    // C / C++ / Objective-C / Objective-C++ (clangd - supports GCC, MSVC, Apple Clang, and Clang toolchains)
    ServerProfile cpp_profile;
    cpp_profile.language_id = "cpp";
    cpp_profile.extensions = {".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".inl", ".m", ".mm"};
    cpp_profile.executable_name = "clangd";
    cpp_profile.default_args = {
        "-j=2",
        "--background-index",
        "--background-index-priority=low",
        "--pch-storage=disk",
        "--limit-results=100",
        "--limit-references=500",
        "--clang-tidy=false",
        "--enable-config",
        "--header-insertion-decorators=false",
        "--query-driver=**",
        "--compile-commands-dir=build",
        "--completion-style=detailed",
        "--all-scopes-completion"
    };
    cpp_profile.root_markers = {"compile_commands.json", "CMakeLists.txt", ".git"};
    register_profile(std::move(cpp_profile));

    // CMake
    ServerProfile cmake_profile;
    cmake_profile.language_id = "cmake";
    cmake_profile.extensions = {".cmake", "cmakelists.txt", "CMakeLists.txt"};
    cmake_profile.executable_name = "cmakels";
    cmake_profile.default_args = {"build"};
    cmake_profile.root_markers = {"CMakeLists.txt", ".git"};
    register_profile(std::move(cmake_profile));

    // Rust (rust-analyzer)
    ServerProfile rust_profile;
    rust_profile.language_id = "rust";
    rust_profile.extensions = {".rs"};
    rust_profile.executable_name = "rust-analyzer";
    rust_profile.default_args = {};
    rust_profile.root_markers = {"Cargo.toml", ".git"};
    register_profile(std::move(rust_profile));

    // Python (pyright / pylsp)
    ServerProfile py_profile;
    py_profile.language_id = "python";
    py_profile.extensions = {".py", ".pyw", ".pyi"};
    py_profile.executable_name = "pyright-langserver";
    py_profile.default_args = {"--stdio"};
    py_profile.root_markers = {"pyproject.toml", "requirements.txt", "setup.py", ".git"};
    register_profile(std::move(py_profile));

    // JavaScript / TypeScript (typescript-language-server / vtsls / deno / bun)
    ServerProfile js_profile;
    js_profile.language_id = "typescript";
    js_profile.extensions = {".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"};
    js_profile.executable_name = "typescript-language-server";
    js_profile.default_args = {"--stdio"};
    js_profile.root_markers = {"tsconfig.json", "jsconfig.json", "package.json", "deno.json", "deno.jsonc", "bun.lockb", ".git"};
    register_profile(std::move(js_profile));

    // Go (gopls)
    ServerProfile go_profile;
    go_profile.language_id = "go";
    go_profile.extensions = {".go"};
    go_profile.executable_name = "gopls";
    go_profile.default_args = {};
    go_profile.root_markers = {"go.mod", "go.work", ".git"};
    register_profile(std::move(go_profile));

    // Zig (zls)
    ServerProfile zig_profile;
    zig_profile.language_id = "zig";
    zig_profile.extensions = {".zig", ".zon"};
    zig_profile.executable_name = "zls";
    zig_profile.default_args = {};
    zig_profile.root_markers = {"build.zig", "build.zig.zon", ".git"};
    register_profile(std::move(zig_profile));

    // Lua (lua-language-server)
    ServerProfile lua_profile;
    lua_profile.language_id = "lua";
    lua_profile.extensions = {".lua"};
    lua_profile.executable_name = "lua-language-server";
    lua_profile.default_args = {};
    lua_profile.root_markers = {".luarc.json", ".git"};
    register_profile(std::move(lua_profile));

    // HTML (vscode-html-language-server)
    ServerProfile html_profile;
    html_profile.language_id = "html";
    html_profile.extensions = {".html", ".htm", ".xhtml"};
    html_profile.executable_name = "vscode-html-language-server";
    html_profile.default_args = {"--stdio"};
    html_profile.root_markers = {"package.json", ".git"};
    register_profile(std::move(html_profile));

    // CSS / SCSS / LESS (vscode-css-language-server)
    ServerProfile css_profile;
    css_profile.language_id = "css";
    css_profile.extensions = {".css", ".scss", ".less"};
    css_profile.executable_name = "vscode-css-language-server";
    css_profile.default_args = {"--stdio"};
    css_profile.root_markers = {"package.json", ".git"};
    register_profile(std::move(css_profile));

    // JSON (vscode-json-language-server)
    ServerProfile json_profile;
    json_profile.language_id = "json";
    json_profile.extensions = {".json", ".jsonc"};
    json_profile.executable_name = "vscode-json-language-server";
    json_profile.default_args = {"--stdio"};
    json_profile.root_markers = {"package.json", ".git"};
    register_profile(std::move(json_profile));

    // YAML (yaml-language-server)
    ServerProfile yaml_profile;
    yaml_profile.language_id = "yaml";
    yaml_profile.extensions = {".yaml", ".yml"};
    yaml_profile.executable_name = "yaml-language-server";
    yaml_profile.default_args = {"--stdio"};
    yaml_profile.root_markers = {".git"};
    register_profile(std::move(yaml_profile));

    // Bash / Shell (bash-language-server)
    ServerProfile bash_profile;
    bash_profile.language_id = "bash";
    bash_profile.extensions = {".sh", ".bash", ".zsh"};
    bash_profile.executable_name = "bash-language-server";
    bash_profile.default_args = {"start"};
    bash_profile.root_markers = {".git"};
    register_profile(std::move(bash_profile));

    // Swift (sourcekit-lsp)
    ServerProfile swift_profile;
    swift_profile.language_id = "swift";
    swift_profile.extensions = {".swift"};
    swift_profile.executable_name = "sourcekit-lsp";
    swift_profile.default_args = {};
    swift_profile.root_markers = {"Package.swift", ".git"};
    register_profile(std::move(swift_profile));

    // C# (csharp-ls)
    ServerProfile csharp_profile;
    csharp_profile.language_id = "csharp";
    csharp_profile.extensions = {".cs"};
    csharp_profile.executable_name = "csharp-ls";
    csharp_profile.default_args = {};
    csharp_profile.root_markers = {".sln", ".csproj", ".git"};
    register_profile(std::move(csharp_profile));

    // Java (jdtls)
    ServerProfile java_profile;
    java_profile.language_id = "java";
    java_profile.extensions = {".java"};
    java_profile.executable_name = "jdtls";
    java_profile.default_args = {};
    java_profile.root_markers = {"pom.xml", "build.gradle", ".git"};
    register_profile(std::move(java_profile));
}

} // namespace Zenvra::Language::Registry
