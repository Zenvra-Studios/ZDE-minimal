#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Zenvra::Terminal
{

struct TerminalExitDecoded
{
    uint32_t exit_code = 0;
    bool is_ntstatus = false;
    bool is_normal = true;
    std::string hex_code;
    std::string summary;
    std::string detail;
    std::string formatted_message;
};

/**
 * @brief Decodes a process exit code into a human-readable diagnosis.
 * 
 * Accurately distinguishes between NTSTATUS exception/crash codes (e.g. 0xC0000005, 0xC0000135)
 * and regular Win32 / application exit codes (0..1024), avoiding misinterpreting process exit
 * code 5 as Win32 ERROR_ACCESS_DENIED.
 *
 * @param exit_code The raw 32-bit exit code returned by GetExitCodeProcess.
 * @param shell_path The path to the executable that was spawned.
 * @param is_conpty True if running under Windows ConPTY pseudoconsole, false if running via pipe.
 * @return TerminalExitDecoded Decoded breakdown with formatted messages.
 */
TerminalExitDecoded decode_terminal_exit(uint32_t exit_code,
                                       const std::filesystem::path& shell_path = {},
                                       bool is_conpty = true);

} // namespace Zenvra::Terminal
