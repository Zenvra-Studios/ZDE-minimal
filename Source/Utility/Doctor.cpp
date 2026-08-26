#include "Utility/Doctor.h"
#include "Terminal/WindowsSecurityProbe.h"
#include "Terminal/TerminalSession.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Zenvra::Utility
{

namespace
{
std::vector<std::string> tail_file(const std::filesystem::path& file_path, std::size_t max_lines = 15)
{
    std::vector<std::string> result;
    std::error_code ec;
    if (!std::filesystem::exists(file_path, ec))
    {
        return result;
    }
    std::ifstream file(file_path);
    if (!file)
    {
        return result;
    }
    std::deque<std::string> ring;
    std::string line;
    while (std::getline(file, line))
    {
        ring.push_back(line);
        if (ring.size() > max_lines)
        {
            ring.pop_front();
        }
    }
    result.assign(ring.begin(), ring.end());
    return result;
}

std::filesystem::path get_temp_log_path(const std::string& filename)
{
    std::error_code ec;
    std::filesystem::path log_path;
#if defined(_WIN32)
    char tmp_buf[512] = {};
    std::size_t tmp_len = 0;
    if (getenv_s(&tmp_len, tmp_buf, sizeof(tmp_buf), "TEMP") == 0 && tmp_len > 0)
    {
        log_path = std::filesystem::path(tmp_buf);
    }
    else
    {
        log_path = std::filesystem::current_path(ec);
    }
#else
    const char* tmp = std::getenv("TMPDIR");
    log_path = tmp ? std::filesystem::path(tmp) : std::filesystem::current_path(ec);
#endif
    log_path /= filename;
    return log_path;
}
} // namespace

std::string Doctor::generate_report()
{
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                     ZDE DOCTOR: SYSTEM & RUNTIME REPORT                        \n";
    ss << "================================================================================\n\n";

    // 1. Antigravity & ZDE Version
    ss << "[ZDE Information]\n";
    ss << "  Application: " << "Zenvra Development Studio (ZDE)\n";
    ss << "  Version:     " << "0.1.0-preview\n";
    ss << "  Architecture:";
#if defined(_WIN32)
    SYSTEM_INFO sys_info{};
    GetNativeSystemInfo(&sys_info);
    if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ss << " ARM64\n";
    else if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ss << " x86_64 (AMD64 / Intel 64-Bit)\n";
    else if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ss << " x86 (32-Bit)\n";
    else ss << " Unknown (" << sys_info.wProcessorArchitecture << ")\n";
#else
    ss << " Native POSIX\n";
#endif

    // 2. OS & Security Probe
    const auto& probe = Terminal::WindowsSecurityProbe::get_cached_info();
    ss << "\n[Operating System & Security Posture]\n";
    if (!probe.product_name.empty())
    {
        ss << "  Product Name:   " << probe.product_name << "\n";
    }
    if (!probe.display_version.empty())
    {
        ss << "  Display Ver:    " << probe.display_version << "\n";
    }
    if (!probe.build_number.empty())
    {
        ss << "  Build Number:   " << probe.build_number;
        if (probe.ubr > 0) ss << "." << probe.ubr;
        ss << "\n";
    }
    ss << "  SAC (Security): " << Terminal::WindowsSecurityProbe::sac_state_to_string(probe.sac_state) << "\n";
    ss << "  WinDefend Svc:  " << (probe.defender_status == Terminal::DefenderServiceStatus::Present ? "Present [OK]" :
                                      probe.defender_status == Terminal::DefenderServiceStatus::MissingOrStripped ? "MISSING / STRIPPED" : "Unknown") << "\n";
    ss << "  .NET Framework: " << (probe.dotnet_installed ? ("Installed (Release DWORD " + std::to_string(probe.dotnet_release) + ") [OK]") : "MISSING OR BROKEN") << "\n";
    ss << "  Classification: " << Terminal::WindowsSecurityProbe::classification_to_string(probe.classification) << "\n";
    if (!probe.classification_reasons.empty())
    {
        ss << "  Issues Detected:\n";
        for (const auto& reason : probe.classification_reasons)
        {
            ss << "    * " << reason << "\n";
        }
    }

    // 3. Terminal & Shell Resolution
    ss << "\n[Terminal Subsystem]\n";
    const auto active_shell = Terminal::TerminalSession::resolve_host_shell();
    ss << "  Active Shell:   " << (active_shell.empty() ? "(None found)" : active_shell.string()) << "\n";

    const char* zde_shell_env = std::getenv("ZDE_SHELL");
    if (zde_shell_env != nullptr && zde_shell_env[0] != '\0')
    {
        ss << "  Env ZDE_SHELL:  " << zde_shell_env << "\n";
    }
    const char* no_conpty_env = std::getenv("ZDE_NO_CONPTY");
    if (no_conpty_env != nullptr)
    {
        ss << "  Env NO_CONPTY:  " << no_conpty_env << "\n";
    }
    const char* liveness_env = std::getenv("ZDE_LIVENESS_MS");
    if (liveness_env != nullptr)
    {
        ss << "  Env LIVENESS_MS:" << liveness_env << "\n";
    }

    // 4. Log files snippet
    const auto term_log_path = get_temp_log_path("zde-terminal.log");
    ss << "\n[Terminal Log: " << term_log_path.string() << "]\n";
    const auto term_lines = tail_file(term_log_path, 10);
    if (term_lines.empty())
    {
        ss << "  (Log is empty or no terminal sessions started yet)\n";
    }
    else
    {
        for (const auto& l : term_lines)
        {
            ss << "  " << l << "\n";
        }
    }

    const auto lsp_log_path = get_temp_log_path("zde-lsp.log");
    ss << "\n[LSP Log: " << lsp_log_path.string() << "]\n";
    const auto lsp_lines = tail_file(lsp_log_path, 10);
    if (lsp_lines.empty())
    {
        ss << "  (Log is empty or no LSP activity logged yet)\n";
    }
    else
    {
        for (const auto& l : lsp_lines)
        {
            ss << "  " << l << "\n";
        }
    }

    ss << "\n================================================================================\n";
    return ss.str();
}

void Doctor::print_report()
{
    std::cout << generate_report() << std::flush;
}

} // namespace Zenvra::Utility
