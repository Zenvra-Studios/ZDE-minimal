#include "Plugins/Toolchain/ToolManager.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace Zenvra::Plugins::Toolchain
{

namespace
{

std::vector<std::string> split_path_env(const char* env_var)
{
    std::vector<std::string> paths;
    if (!env_var) return paths;

#if defined(_WIN32)
    const char delimiter = ';';
#else
    const char delimiter = ':';
#endif

    std::string s(env_var);
    std::size_t start = 0;
    std::size_t end = s.find(delimiter);

    while (end != std::string::npos)
    {
        std::string token = s.substr(start, end - start);
        if (!token.empty()) paths.push_back(token);
        start = end + 1;
        end = s.find(delimiter, start);
    }
    std::string last = s.substr(start);
    if (!last.empty()) paths.push_back(last);

    return paths;
}

std::filesystem::path get_user_home()
{
#if defined(_WIN32)
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile) return userprofile;
    const char* homedrive = std::getenv("HOMEDRIVE");
    const char* homepath = std::getenv("HOMEPATH");
    if (homedrive && homepath) return std::string(homedrive) + homepath;
    return "C:\\";
#else
    const char* home = std::getenv("HOME");
    if (home) return home;
    return "/tmp";
#endif
}

std::string sanitize_version_line(std::string_view raw)
{
    if (raw.empty()) return "unknown";
    std::size_t end = raw.find('\n');
    if (end != std::string::npos) raw = raw.substr(0, end);
    end = raw.find('\r');
    if (end != std::string::npos) raw = raw.substr(0, end);

    // Trim leading and trailing whitespace
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
    return std::string(raw);
}

} // namespace

ToolManager& ToolManager::instance() noexcept
{
    static ToolManager inst;
    return inst;
}

ToolManager::ToolManager() = default;

void ToolManager::initialize(const std::filesystem::path& zde_home_dir)
{
    m_managed_tools_dir = zde_home_dir / "tools";
    m_registry_file = zde_home_dir / "registry" / "tools.json";

    std::error_code ec;
    std::filesystem::create_directories(m_managed_tools_dir, ec);
    std::filesystem::create_directories(m_registry_file.parent_path(), ec);

    m_registry.load_from_disk(m_registry_file);
}

std::optional<std::filesystem::path> ToolManager::find_in_project(
    std::string_view executable_name,
    const std::filesystem::path& workspace_root) const
{
    if (workspace_root.empty()) return std::nullopt;

    std::error_code ec;
    if (!std::filesystem::is_directory(workspace_root, ec)) return std::nullopt;

    std::vector<std::filesystem::path> candidate_dirs = {
        workspace_root / "node_modules" / ".bin",
#if defined(_WIN32)
        workspace_root / ".venv" / "Scripts",
        workspace_root / "venv" / "Scripts",
        workspace_root / "env" / "Scripts",
#else
        workspace_root / ".venv" / "bin",
        workspace_root / "venv" / "bin",
        workspace_root / "env" / "bin",
#endif
        workspace_root / "target" / "debug",
        workspace_root / "target" / "release",
        workspace_root / "build",
        workspace_root / "bin"
    };

    for (const auto& dir : candidate_dirs)
    {
        std::filesystem::path p = dir / executable_name;
        if (std::filesystem::exists(p, ec)) return p;
#if defined(_WIN32)
        std::filesystem::path p_exe = dir / (std::string(executable_name) + ".exe");
        if (std::filesystem::exists(p_exe, ec)) return p_exe;
        std::filesystem::path p_cmd = dir / (std::string(executable_name) + ".cmd");
        if (std::filesystem::exists(p_cmd, ec)) return p_cmd;
#endif
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ToolManager::find_in_managed(std::string_view tool_id) const
{
    if (m_managed_tools_dir.empty()) return std::nullopt;

    std::error_code ec;
    std::filesystem::path tool_dir = m_managed_tools_dir / tool_id;
    if (!std::filesystem::is_directory(tool_dir, ec)) return std::nullopt;

    // Check directly in tool_dir or in tool_dir/bin
    std::vector<std::filesystem::path> candidates = {
        tool_dir / tool_id,
        tool_dir / "bin" / tool_id
    };

    for (const auto& c : candidates)
    {
        if (std::filesystem::exists(c, ec)) return c;
#if defined(_WIN32)
        std::filesystem::path c_exe = c.string() + ".exe";
        if (std::filesystem::exists(c_exe, ec)) return c_exe;
#endif
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> ToolManager::find_in_system(std::string_view executable_name) const
{
    std::error_code ec;

    // 1. Scan PATH environment variable
    const char* path_env = std::getenv("PATH");
    std::vector<std::string> path_dirs = split_path_env(path_env);

    std::vector<std::string> extensions = {""};
#if defined(_WIN32)
    extensions = {".exe", ".cmd", ".bat", ""};
#endif

    for (const auto& dir_str : path_dirs)
    {
        std::filesystem::path dir(dir_str);
        for (const auto& ext : extensions)
        {
            std::filesystem::path target = dir / (std::string(executable_name) + ext);
            if (std::filesystem::exists(target, ec) && !std::filesystem::is_directory(target, ec))
            {
                return target;
            }
        }
    }

    // 2. Scan standard installation locations for popular tools
    std::filesystem::path home = get_user_home();
    std::vector<std::filesystem::path> standard_candidates;

    if (executable_name == "clangd")
    {
#if defined(_WIN32)
        standard_candidates.push_back("C:\\Program Files\\LLVM\\bin\\clangd.exe");
        standard_candidates.push_back("C:\\LLVM\\bin\\clangd.exe");
        standard_candidates.push_back("C:\\msys64\\ucrt64\\bin\\clangd.exe");
        standard_candidates.push_back("C:\\msys64\\mingw64\\bin\\clangd.exe");
        standard_candidates.push_back("C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\Llvm\\bin\\clangd.exe");
        standard_candidates.push_back("C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\Llvm\\bin\\clangd.exe");
#else
        standard_candidates.push_back("/usr/bin/clangd");
        standard_candidates.push_back("/usr/local/bin/clangd");
        standard_candidates.push_back("/opt/homebrew/bin/clangd");
#endif
    }
    else if (executable_name == "rust-analyzer" || executable_name == "cargo" || executable_name == "rustc")
    {
#if defined(_WIN32)
        standard_candidates.push_back(home / ".cargo" / "bin" / (std::string(executable_name) + ".exe"));
#else
        standard_candidates.push_back(home / ".cargo" / "bin" / executable_name);
        standard_candidates.push_back("/usr/bin" / std::filesystem::path(executable_name));
        standard_candidates.push_back("/usr/local/bin" / std::filesystem::path(executable_name));
        standard_candidates.push_back("/opt/homebrew/bin" / std::filesystem::path(executable_name));
#endif
    }
    else if (executable_name == "bun")
    {
#if defined(_WIN32)
        standard_candidates.push_back(home / ".bun" / "bin" / "bun.exe");
#else
        standard_candidates.push_back(home / ".bun" / "bin" / "bun");
        standard_candidates.push_back("/opt/homebrew/bin/bun");
        standard_candidates.push_back("/usr/local/bin/bun");
#endif
    }
    else if (executable_name == "node")
    {
#if defined(_WIN32)
        standard_candidates.push_back("C:\\Program Files\\nodejs\\node.exe");
        standard_candidates.push_back("C:\\Program Files (x86)\\nodejs\\node.exe");
        standard_candidates.push_back(home / "AppData" / "Roaming" / "nvm" / "node.exe");
#else
        standard_candidates.push_back("/usr/bin/node");
        standard_candidates.push_back("/usr/local/bin/node");
        standard_candidates.push_back("/opt/homebrew/bin/node");
#endif
    }
    else if (executable_name == "ninja")
    {
#if defined(_WIN32)
        standard_candidates.push_back("C:\\Program Files\\Ninja\\ninja.exe");
        standard_candidates.push_back("C:\\ninja\\ninja.exe");
#else
        standard_candidates.push_back("/usr/bin/ninja");
        standard_candidates.push_back("/usr/local/bin/ninja");
        standard_candidates.push_back("/opt/homebrew/bin/ninja");
#endif
    }

    for (const auto& cand : standard_candidates)
    {
        if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec))
        {
            return cand;
        }
    }

    return std::nullopt;
}

std::string ToolManager::query_version(const std::filesystem::path& executable_path) const
{
    if (executable_path.empty()) return "unknown";

    std::ostringstream cmd;
#if defined(_WIN32)
    cmd << "cmd.exe /d /c \"\"" << executable_path.string() << "\" --version 2>&1\"";
    FILE* pipe = _popen(cmd.str().c_str(), "r");
#else
    cmd << "\"" << executable_path.string() << "\" --version 2>&1";
    FILE* pipe = popen(cmd.str().c_str(), "r");
#endif

    if (!pipe) return "unknown";

    std::array<char, 256> buffer{};
    std::string output;
    if (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        output = buffer.data();
    }

#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return sanitize_version_line(output);
}

std::optional<ToolInfo> ToolManager::resolve_tool(
    std::string_view tool_id,
    const std::filesystem::path& workspace_root)
{
    // Check cached in registry first
    if (auto cached = m_registry.get_tool(tool_id))
    {
        std::error_code ec;
        if (cached->status == ToolStatus::Available && std::filesystem::exists(cached->executable_path, ec))
        {
            return cached;
        }
    }

    ToolInfo info;
    info.id = std::string(tool_id);
    info.name = std::string(tool_id);

    // Tier 1: Project-local tool
    if (auto p = find_in_project(tool_id, workspace_root))
    {
        info.executable_path = *p;
        info.source = ToolSource::Project;
        info.status = ToolStatus::Available;
        info.version = query_version(*p);
        m_registry.register_tool(info);
        if (!m_registry_file.empty()) m_registry.save_to_disk(m_registry_file);
        return info;
    }

    // Tier 2: ZDE Managed tool (~/.zde/tools/)
    if (auto p = find_in_managed(tool_id))
    {
        info.executable_path = *p;
        info.source = ToolSource::Managed;
        info.status = ToolStatus::Available;
        info.version = query_version(*p);
        m_registry.register_tool(info);
        if (!m_registry_file.empty()) m_registry.save_to_disk(m_registry_file);
        return info;
    }

    // Tier 3: System PATH & standard locations
    if (auto p = find_in_system(tool_id))
    {
        info.executable_path = *p;
        info.source = ToolSource::System;
        info.status = ToolStatus::Available;
        info.version = query_version(*p);
        m_registry.register_tool(info);
        if (!m_registry_file.empty()) m_registry.save_to_disk(m_registry_file);
        return info;
    }

    // Tier 4: Fallback / Not Found
    info.status = ToolStatus::Missing;
    m_registry.register_tool(info);
    if (!m_registry_file.empty()) m_registry.save_to_disk(m_registry_file);
    return std::nullopt;
}

void ToolManager::scan_and_register_known_tools(const std::filesystem::path& workspace_root)
{
    static const std::vector<std::string_view> known_tools = {
        "clangd",
        "rustc",
        "cargo",
        "rust-analyzer",
        "bun",
        "node",
        "ninja",
        "cmake",
        "gopls",
        "pyright"
    };

    for (const auto& tid : known_tools)
    {
        static_cast<void>(resolve_tool(tid, workspace_root));
    }
}

} // namespace Zenvra::Plugins::Toolchain
