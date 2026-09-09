#include "Language/Registry/ServerRegistry.h"

#include <array>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

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
    static auto* registry = new ServerRegistry();
    return *registry;
}

ServerRegistry::ServerRegistry()
{
    // Profiles are registered dynamically from installed and enabled plugins
}

void ServerRegistry::register_profile(ServerProfile profile)
{
    for (const auto& ext : profile.extensions)
    {
        m_language_by_extension[ext] = profile.language_id;
    }
    m_profiles_by_language[profile.language_id] = std::move(profile);
}

void ServerRegistry::unregister_profile(std::string_view language_id)
{
    std::string lid(language_id);
    auto it = m_profiles_by_language.find(lid);
    if (it != m_profiles_by_language.end())
    {
        for (const auto& ext : it->second.extensions)
        {
            if (auto ext_it = m_language_by_extension.find(ext); ext_it != m_language_by_extension.end() && ext_it->second == lid)
            {
                m_language_by_extension.erase(ext_it);
            }
        }
        m_profiles_by_language.erase(it);
    }
    clear_cache();
}

void ServerRegistry::unregister_profiles_for_plugin(std::string_view plugin_id)
{
    if (plugin_id.empty()) return;
    std::vector<std::string> to_remove;
    for (const auto& [lang, prof] : m_profiles_by_language)
    {
        if (prof.plugin_id == plugin_id)
        {
            to_remove.push_back(lang);
        }
    }
    for (const auto& lang : to_remove)
    {
        unregister_profile(lang);
    }
}

bool ServerRegistry::has_profile_for_language(std::string_view language_id) const noexcept
{
    return m_profiles_by_language.contains(std::string(language_id));
}

bool ServerRegistry::has_profile_for_filename(std::string_view filename) const noexcept
{
    return find_profile_for_filename(filename) != nullptr;
}

void ServerRegistry::clear_all_profiles() noexcept
{
    m_profiles_by_language.clear();
    m_language_by_extension.clear();
    clear_cache();
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

    // 4. Check for C++ STL standard headers without extension
    static const std::unordered_set<std::string_view> s_stl_header_names = {
        "algorithm", "any", "array", "atomic", "barrier", "bit", "bitset", "charconv",
        "chrono", "codecvt", "compare", "complex", "concepts", "condition_variable",
        "coroutine", "csetjmp", "csignal", "cstdarg", "cstddef", "cstdint", "cstdio",
        "cstdlib", "cstring", "ctime", "cuchar", "cwchar", "cwctype", "deque",
        "exception", "execution", "expected", "filesystem", "flat_map", "flat_set",
        "format", "forward_list", "fstream", "functional", "future", "generator",
        "initializer_list", "iomanip", "ios", "iosfwd", "iostream", "istream",
        "iterator", "latch", "limits", "list", "locale", "map", "mdspan", "memory",
        "memory_resource", "mutex", "new", "numbers", "numeric", "optional", "ostream",
        "print", "queue", "random", "ranges", "ratio", "regex", "scoped_allocator",
        "semaphore", "set", "shared_mutex", "source_location", "span", "spanstream",
        "sstream", "stack", "stacktrace", "stdexcept", "stdfloat", "stop_token",
        "streambuf", "string", "string_view", "strstream", "syncstream", "system_error",
        "text_encoding", "thread", "tuple", "typeindex", "typeinfo", "type_traits",
        "unordered_map", "unordered_set", "utility", "valarray", "variant", "vector",
        "version",
        "xstring", "xmemory", "xutility", "xtr1common", "xatomic.h", "xiosbase",
        "xlocale", "xlocinfo", "xlocmon", "xlocnum", "xloctime", "xlocmes",
        "xnode_handle.h", "xpolymorphic_allocator.h", "xstddef", "xthreads.h",
        "xatomic", "yvals", "yvals_core.h", "crtdbg.h", "corecrt.h",
        "c++config.h", "stl_algobase.h", "stl_vector.h", "stl_construct.h",
        "stl_uninitialized.h", "stl_tree.h", "stl_map.h", "stl_set.h",
        "stl_list.h", "stl_deque.h", "stl_queue.h", "stl_stack.h", "stl_pair.h",
        "stl_function.h", "stl_iterator.h", "stl_raw_storage_iter.h",
        "stl_tempbuf.h", "basic_string.h", "basic_string.tcc"
    };

    if (s_stl_header_names.contains(base_lower))
    {
        return find_profile_for_language("cpp");
    }

    const std::string full_path = p.generic_string();
    if (full_path.find("/include/c++/") != std::string::npos ||
        full_path.find("/VC/Tools/MSVC/") != std::string::npos ||
        full_path.find("/usr/include/c++/") != std::string::npos ||
        full_path.find("/usr/include/x86_64-linux-gnu/c++/") != std::string::npos)
    {
        return find_profile_for_language("cpp");
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
    {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        if (auto it = m_executable_cache.find(exe_str); it != m_executable_cache.end())
        {
            return it->second;
        }
    }

    auto cache_and_return = [this, &exe_str](std::filesystem::path result) -> std::filesystem::path {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        m_executable_cache[exe_str] = result;
        return result;
    };

    std::vector<std::string> candidate_names = { exe_str };

    if (exe_str == "cmake-language-server" || exe_str == "cmake-ls" || exe_str == "cmakels" || exe_str == "cmakels-win64" || exe_str == "neocmakelsp")
    {
        candidate_names = { "cmakels", "cmake-language-server", "neocmakelsp", "cmake-ls", "cmakels-win64" };
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
    else if (exe_str == "emmet-ls" || exe_str == "vscode-html-language-server" || exe_str == "html-languageserver" || exe_str == "html")
    {
        candidate_names = { "emmet-ls", "vscode-html-language-server", "html-languageserver", "html" };
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
    else if (exe_str == "gopls" || exe_str == "gopls-v0.23.0" || exe_str == "golang" || exe_str == "go")
    {
        candidate_names = { "gopls", "gopls-v0.23.0", "gopls.exe", "gopls-v0.23.0.exe" };
    }
    else if (exe_str == "asm-lsp" || exe_str == "asm_lsp" || exe_str == "nasm")
    {
        candidate_names = { "asm-lsp", "asm_lsp", "nasm" };
    }
    else if (exe_str == "phpantom_lsp" || exe_str == "phpantom" || exe_str == "phpatom" ||
             exe_str == "php-ls" || exe_str == "intelephense" || exe_str == "phpactor" ||
             exe_str == "php-language-server")
    {
        candidate_names = { "phpantom_lsp", "phpantom", "phpatom", "php-ls", "phpactor", "intelephense", "php-language-server" };
    }
    else if (exe_str == "shader-language-server" || exe_str == "shader-ls" || exe_str == "shaderserver")
    {
        candidate_names = { "shader-language-server", "shader-ls", "shaderserver" };
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
                app_dir / "plugins" / "lsp" / exe_with_ext,
                app_dir / "plugins" / "lsp" / cur_name / exe_with_ext,
                app_dir / "plugins" / "lsp" / cur_name / "bin" / exe_with_ext,
                app_dir / "plugins" / exe_with_ext,
                app_dir / "bin" / exe_with_ext,
                app_dir / exe_with_ext,
            };

            for (const auto& candidate : app_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return cache_and_return(candidate);
                }
            }

            // Check relative plugins directory up to 6 levels above executable
            std::filesystem::path check_dir = app_dir;
            for (int i = 0; i < 6; ++i)
            {
                const std::filesystem::path direct_candidates[] = {
                    check_dir / "plugins" / "lsp" / exe_with_ext,
                    check_dir / "plugins" / "lsp" / cur_name / exe_with_ext,
                    check_dir / "plugins" / "lsp" / cur_name / "bin" / exe_with_ext,
                    check_dir / "plugins" / "tools" / cur_name / exe_with_ext,
                    check_dir / "plugins" / "tools" / cur_name / "bin" / exe_with_ext,
                    check_dir / "plugins" / "lsp" / "shader-ls" / "win" / exe_with_ext,
                    check_dir / "plugins" / "lsp" / "shader-ls" / exe_with_ext,
                    check_dir / "plugins" / exe_with_ext,
                    check_dir / "bin" / exe_with_ext,
                };
                for (const auto& candidate : direct_candidates)
                {
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                    {
                        return cache_and_return(candidate);
                    }
                }

                const std::filesystem::path plugin_dirs[] = {
                    check_dir / "plugins" / "lsp",
                    check_dir / "plugins" / "tools",
                    check_dir / "plugins",
                };
                for (const auto& pdir : plugin_dirs)
                {
                    std::error_code ec;
                    if (std::filesystem::exists(pdir, ec) && std::filesystem::is_directory(pdir, ec))
                    {
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(pdir, ec))
                        {
                            if (entry.is_regular_file())
                            {
                                const std::string filename = entry.path().filename().string();
                                if (filename == exe_with_ext || filename == cur_name)
                                {
                                    return cache_and_return(entry.path());
                                }
                                if (cur_name.starts_with("gopls") && filename.starts_with("gopls") && filename.ends_with(".exe"))
                                {
                                    return cache_and_return(entry.path());
                                }
                                if ((cur_name.starts_with("phpantom") || cur_name.starts_with("phpatom") || cur_name.starts_with("php")) &&
                                    (filename == "phpantom_lsp.exe" || filename == "php-ls.exe"))
                                {
                                    return cache_and_return(entry.path());
                                }
                                if ((cur_name.starts_with("cmake") || cur_name == "cmakels") &&
                                    (filename == "cmakels-win64.exe" || filename == "cmakels.exe" || filename == "cmake-language-server.exe"))
                                {
                                    return cache_and_return(entry.path());
                                }
                                if ((cur_name.starts_with("shader") || cur_name == "shader-language-server" || cur_name == "shader-ls") &&
                                    (filename == "shader-language-server.exe" || filename == "shader-language-server"))
                                {
                                    return cache_and_return(entry.path());
                                }
                            }
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
                app_dir / "bin" / cur_name,
                app_dir / cur_name,
            };
            for (const auto& candidate : app_candidates)
            {
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return cache_and_return(candidate);
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
                    return cache_and_return(candidate);
                }
            }
        }
#endif

        // 0b. Traverse upward from current directory and check candidate folders (plugins, etc.)
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
                base / "plugins" / "lsp" / cur_name / exe_with_ext,
                base / "plugins" / "lsp" / cur_name / "bin" / exe_with_ext,
                base / "plugins" / "tools" / cur_name / exe_with_ext,
                base / "plugins" / "tools" / cur_name / "bin" / exe_with_ext,
                base / "plugins" / exe_with_ext,
                base / exe_with_ext,
            };

            for (const auto& candidate : direct_candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                {
                    return cache_and_return(candidate);
                }
            }

            // Check plugins subdirectories
            const std::filesystem::path container_dirs[] = {
                base / "plugins" / "lsp",
                base / "plugins" / "tools",
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
                                return cache_and_return(sub_candidate);
                            }
                            const auto sub_bin = entry.path() / "bin" / exe_with_ext;
                            if (std::filesystem::exists(sub_bin, ec) && std::filesystem::is_regular_file(sub_bin, ec))
                            {
                                return cache_and_return(sub_bin);
                            }
                            for (const auto& sub_file : std::filesystem::directory_iterator(entry.path(), ec))
                            {
                                if (sub_file.is_regular_file(ec))
                                {
                                    const std::string fn = sub_file.path().filename().string();
                                    if (fn == exe_with_ext || fn == cur_name)
                                    {
                                        return cache_and_return(sub_file.path());
                                    }
                                    if (cur_name.starts_with("gopls") && fn.starts_with("gopls") && fn.ends_with(".exe"))
                                    {
                                        return cache_and_return(sub_file.path());
                                    }
                                    if ((cur_name.starts_with("cmake") || cur_name == "cmakels") &&
                                        (fn == "cmakels-win64.exe" || fn == "cmakels.exe" || fn == "cmake-language-server.exe"))
                                    {
                                        return cache_and_return(sub_file.path());
                                    }
                                    if ((cur_name.starts_with("phpantom") || cur_name.starts_with("phpatom") || cur_name.starts_with("php")) &&
                                        (fn == "phpantom_lsp.exe" || fn == "php-ls.exe"))
                                    {
                                        return cache_and_return(sub_file.path());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

#if defined(_WIN32)
        // 2. Second Priority (Windows): Check Scoop, WinGet, Chocolatey, NuGet, and LocalAppData environment paths
        std::wstring user_profile_w;
        wchar_t up_buf[32768]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", up_buf, 32768) > 0)
        {
            user_profile_w = up_buf;
        }
        else if (const char* env_up = std::getenv("USERPROFILE"))
        {
            user_profile_w = std::wstring(env_up, env_up + strlen(env_up));
        }

        const char* local_appdata = std::getenv("LOCALAPPDATA");
        const char* appdata = std::getenv("APPDATA");
        const char* program_data = std::getenv("ProgramData");

        if (!user_profile_w.empty())
        {
            const std::filesystem::path up(user_profile_w);
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
                    return cache_and_return(candidate);
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
                    return cache_and_return(candidate);
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
                    return cache_and_return(candidate);
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
                return cache_and_return(npm_candidate);
            }
        }

        // 3. Third Priority: SearchPathW (System Windows PATH search)
        std::wstring exe_w(exe_with_ext.begin(), exe_with_ext.end());
        std::array<wchar_t, 32768> resolved{};
        const DWORD length = SearchPathW(
            nullptr, exe_w.c_str(), nullptr, static_cast<DWORD>(resolved.size()), resolved.data(), nullptr);
        if (length > 0 && length < resolved.size())
        {
            return cache_and_return(std::filesystem::path(resolved.data()));
        }

        // 4. Fourth Priority: Program Files LLVM / CMake, and dynamic Visual Studio installation scanning
        const std::filesystem::path standard_program_files[] = {
            std::filesystem::path("C:/Program Files/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/LLVM/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/Program Files (x86)/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/CMake/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/clang64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/ucrt64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/mingw64/bin") / exe_with_ext,
            std::filesystem::path("C:/msys64/usr/bin") / exe_with_ext,
            std::filesystem::path("C:/MinGW/bin") / exe_with_ext,
        };
        for (const auto& candidate : standard_program_files)
        {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                return cache_and_return(candidate);
            }
        }

        // Dynamically inspect any Visual Studio version/edition (2022, 2019, Preview, BuildTools, etc.)
        const std::filesystem::path vs_roots[] = {
            "C:/Program Files/Microsoft Visual Studio",
            "C:/Program Files (x86)/Microsoft Visual Studio",
        };
        for (const auto& vs_root : vs_roots)
        {
            std::error_code ec;
            if (std::filesystem::exists(vs_root, ec) && std::filesystem::is_directory(vs_root, ec))
            {
                for (const auto& year_entry : std::filesystem::directory_iterator(vs_root, ec))
                {
                    if (!year_entry.is_directory(ec)) continue;
                    for (const auto& edition_entry : std::filesystem::directory_iterator(year_entry.path(), ec))
                    {
                        if (!edition_entry.is_directory(ec)) continue;
                        
                        // Check Visual Studio Bundled LLVM
                        const std::filesystem::path vs_llvm_candidates[] = {
                            edition_entry.path() / "VC/Tools/Llvm/bin" / exe_with_ext,
                            edition_entry.path() / "VC/Tools/Llvm/x64/bin" / exe_with_ext,
                        };
                        for (const auto& c : vs_llvm_candidates)
                        {
                            if (std::filesystem::exists(c, ec) && std::filesystem::is_regular_file(c, ec))
                            {
                                return cache_and_return(c);
                            }
                        }

                        // Check MSVC compiler tools dynamically across all installed MSVC versions
                        const auto msvc_tools_dir = edition_entry.path() / "VC/Tools/MSVC";
                        if (std::filesystem::exists(msvc_tools_dir, ec) && std::filesystem::is_directory(msvc_tools_dir, ec))
                        {
                            for (const auto& msvc_ver : std::filesystem::directory_iterator(msvc_tools_dir, ec))
                            {
                                if (msvc_ver.is_directory(ec))
                                {
                                    const std::filesystem::path msvc_bins[] = {
                                        msvc_ver.path() / "bin/Hostx64/x64" / exe_with_ext,
                                        msvc_ver.path() / "bin/Hostx86/x64" / exe_with_ext,
                                        msvc_ver.path() / "bin/Hostx86/x86" / exe_with_ext,
                                    };
                                    for (const auto& b : msvc_bins)
                                    {
                                        if (std::filesystem::exists(b, ec) && std::filesystem::is_regular_file(b, ec))
                                        {
                                            return cache_and_return(b);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
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
                    return cache_and_return(candidate);
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
                return cache_and_return(candidate);
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
                    return cache_and_return(p);
                }
            }
        }
#endif
    }

    return cache_and_return({});
}

void ServerRegistry::clear_cache() noexcept
{
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    m_executable_cache.clear();
}

std::optional<ServerProfile> ServerRegistry::create_standard_profile_for(std::string_view language_or_tool)
{
    if (language_or_tool.empty()) return std::nullopt;

    std::string key(language_or_tool);
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // 1. C / C++ (clangd)
    if (key == "cpp" || key == "c" || key == "clangd" || key == "vscode-clangd" ||
        key.find("clangd") != std::string::npos || key.find("cpp") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "cpp";
        p.extensions = {".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".hh", ".hxx", ".inl", ".m", ".mm"};
        p.executable_name = "clangd";
        p.default_args = {
            "-j=4",
            "--background-index",
            "--background-index-priority=normal",
            "--pch-storage=memory",
            "--limit-results=100",
            "--limit-references=500",
            "--clang-tidy=false",
            "--enable-config",
            "--header-insertion=iwyu",
            "--header-insertion-decorators=false",
            "--query-driver=*,*/*,**/*,C:/*,C:/**,D:/*,D:/**,E:/*,E:/**,/usr/**,/opt/**",
            "--completion-style=detailed",
            "--all-scopes-completion"
        };
        p.root_markers = {"compile_commands.json", "CMakeLists.txt", ".git"};
        return p;
    }

    // 2. Rust (rust-analyzer)
    if (key == "rust" || key == "rust-analyzer" || key == "cargo" ||
        key.find("rust") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "rust";
        p.extensions = {".rs"};
        p.executable_name = "rust-analyzer";
        p.default_args = {};
        p.root_markers = {"Cargo.toml", ".git"};
        return p;
    }

    // 3. Go (gopls)
    if (key == "go" || key == "golang" || key == "gopls" ||
        key.find("golang") != std::string::npos || key.find("gopls") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "go";
        p.extensions = {".go", ".mod", ".work"};
        p.executable_name = "gopls";
        p.default_args = {};
        p.root_markers = {"go.mod", "go.work", ".git"};
        return p;
    }

    // 4. Python (pyright / pylsp)
    if (key == "python" || key == "pyright" || key == "pyright-langserver" || key == "pylsp" ||
        key.find("python") != std::string::npos || key.find("pyright") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "python";
        p.extensions = {".py", ".pyw", ".pyi"};
        p.executable_name = "pyright-langserver";
        p.default_args = {"--stdio"};
        p.root_markers = {"pyproject.toml", "requirements.txt", "setup.py", ".git"};
        return p;
    }

    // 5. Ruby (ruby-lsp / solargraph)
    if (key == "ruby" || key == "ruby-lsp" || key == "solargraph" ||
        key.find("ruby") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "ruby";
        p.extensions = {".rb", ".rake", "Gemfile"};
        p.executable_name = "ruby-lsp";
        p.default_args = {"stdio"};
        p.root_markers = {"Gemfile", ".git"};
        return p;
    }

    // 6. CMake (cmake-language-server / neocmakelsp / cmakels)
    if (key == "cmake" || key == "cmake-tools" || key == "cmake-language-server" ||
        key.find("cmake") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "cmake";
        p.extensions = {".cmake", "cmakelists.txt", "CMakeLists.txt"};
        p.executable_name = "cmake-language-server";
        p.default_args = {};
        p.root_markers = {"CMakeLists.txt", ".git"};
        return p;
    }

    // 7. Zig (zls)
    if (key == "zig" || key == "zls" || key.find("zig") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "zig";
        p.extensions = {".zig", ".zon"};
        p.executable_name = "zls";
        p.default_args = {};
        p.root_markers = {"build.zig", "build.zig.zon", ".git"};
        return p;
    }

    // 8. JavaScript / TypeScript (typescript-language-server)
    if (key == "typescript" || key == "javascript" || key == "ts" || key == "js" ||
        key.find("typescript") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "typescript";
        p.extensions = {".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"};
        p.executable_name = "typescript-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"tsconfig.json", "jsconfig.json", "package.json", ".git"};
        return p;
    }

    // 9. Lua (lua-language-server)
    if (key == "lua" || key.find("lua") != std::string::npos)
    {
        ServerProfile p;
        p.language_id = "lua";
        p.extensions = {".lua"};
        p.executable_name = "lua-language-server";
        p.default_args = {};
        p.root_markers = {".luarc.json", ".git"};
        return p;
    }

    // 10. HTML / CSS / JSON / YAML
    if (key == "html")
    {
        ServerProfile p;
        p.language_id = "html";
        p.extensions = {".html", ".htm", ".xhtml"};
        p.executable_name = "emmet-ls";
        p.default_args = {"--stdio"};
        p.root_markers = {"package.json", ".git"};
        return p;
    }
    if (key == "css")
    {
        ServerProfile p;
        p.language_id = "css";
        p.extensions = {".css", ".scss", ".less"};
        p.executable_name = "vscode-css-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"package.json", ".git"};
        return p;
    }
    if (key == "json")
    {
        ServerProfile p;
        p.language_id = "json";
        p.extensions = {".json", ".jsonc"};
        p.executable_name = "vscode-json-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"package.json", ".git"};
        return p;
    }
    if (key == "yaml")
    {
        ServerProfile p;
        p.language_id = "yaml";
        p.extensions = {".yaml", ".yml"};
        p.executable_name = "yaml-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {".git"};
        return p;
    }

    // 11. Bash / Shell
    if (key == "bash" || key == "sh")
    {
        ServerProfile p;
        p.language_id = "bash";
        p.extensions = {".sh", ".bash", ".zsh"};
        p.executable_name = "bash-language-server";
        p.default_args = {"start"};
        p.root_markers = {".git"};
        return p;
    }

    // 12. Swift
    if (key == "swift")
    {
        ServerProfile p;
        p.language_id = "swift";
        p.extensions = {".swift"};
        p.executable_name = "sourcekit-lsp";
        p.default_args = {};
        p.root_markers = {"Package.swift", ".git"};
        return p;
    }

    // 13. C#
    if (key == "csharp" || key == "cs")
    {
        ServerProfile p;
        p.language_id = "csharp";
        p.extensions = {".cs"};
        p.executable_name = "csharp-ls";
        p.default_args = {};
        p.root_markers = {".sln", ".csproj", ".git"};
        return p;
    }

    // 14. Java
    if (key == "java")
    {
        ServerProfile p;
        p.language_id = "java";
        p.extensions = {".java"};
        p.executable_name = "jdtls";
        p.default_args = {};
        p.root_markers = {"pom.xml", "build.gradle", ".git"};
        return p;
    }

    // 15. Assembly
    if (key == "asm")
    {
        ServerProfile p;
        p.language_id = "asm";
        p.extensions = {".asm", ".s", ".S", ".nasm", ".inc", ".a51"};
        p.executable_name = "asm-lsp";
        p.default_args = {};
        p.root_markers = {"Makefile", "CMakeLists.txt", ".git"};
        return p;
    }

    // 16. PHP
    if (key == "php")
    {
        ServerProfile p;
        p.language_id = "php";
        p.extensions = {".php", ".phtml", ".php4", ".php5", ".php7", ".php8", ".phps"};
        p.executable_name = "phpantom_lsp";
        p.default_args = {"--stdio"};
        p.root_markers = {"composer.json", ".phpantom.toml", "artisan", ".git"};
        return p;
    }

    // 17. Shaders - GLSL
    if (key == "glsl" || key == "shader")
    {
        ServerProfile p;
        p.language_id = "glsl";
        p.extensions = {
            ".glsl", ".frag", ".vert", ".comp", ".geom", ".tesc", ".tese",
            ".mesh", ".task", ".rgen", ".rint", ".rahit", ".rchit", ".rmiss", ".rcall",
            ".fs", ".vs", ".shader"
        };
        p.executable_name = "shader-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"compile_commands.json", "CMakeLists.txt", ".git"};
        return p;
    }

    // 18. Shaders - HLSL
    if (key == "hlsl")
    {
        ServerProfile p;
        p.language_id = "hlsl";
        p.extensions = {".hlsl", ".hlsli", ".fx", ".fxh"};
        p.executable_name = "shader-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"compile_commands.json", "CMakeLists.txt", ".git"};
        return p;
    }

    // 19. Shaders - WGSL
    if (key == "wgsl")
    {
        ServerProfile p;
        p.language_id = "wgsl";
        p.extensions = {".wgsl"};
        p.executable_name = "shader-language-server";
        p.default_args = {"--stdio"};
        p.root_markers = {"package.json", "Cargo.toml", ".git"};
        return p;
    }

    return std::nullopt;
}

void ServerRegistry::initialize_default_profiles()
{
    const std::string_view defaults[] = {
        "cpp", "cmake", "rust", "python", "typescript", "go", "zig",
        "ruby", "lua", "html", "css", "json", "yaml", "bash", "swift",
        "csharp", "java", "asm", "php", "glsl", "hlsl", "wgsl"
    };

    for (const auto& d : defaults)
    {
        if (auto p = create_standard_profile_for(d))
        {
            register_profile(std::move(*p));
        }
    }
}

} // namespace Zenvra::Language::Registry
