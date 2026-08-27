#include "TerminalExitDecoder.h"

#include <iomanip>
#include <sstream>

namespace Zenvra::Terminal
{

namespace
{
std::string to_hex_string(uint32_t code)
{
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(8) << code;
    return ss.str();
}
} // namespace

TerminalExitDecoded decode_terminal_exit(uint32_t exit_code,
                                       const std::filesystem::path& shell_path,
                                       bool is_conpty)
{
    TerminalExitDecoded result;
    result.exit_code = exit_code;
    result.hex_code = to_hex_string(exit_code);

    if (exit_code == 0)
    {
        result.is_normal = true;
        result.is_ntstatus = false;
        result.summary = "Normal exit (code 0)";
        result.detail = "";
        result.formatted_message = "[Process exited with code 0]";
        return result;
    }

    // Pseudoconsole closed normally upon tab destruction or EOF
    if (exit_code == 0xC0000B5B)
    {
        result.is_normal = true;
        result.is_ntstatus = true;
        result.summary = "STATUS_CONSOLE_CLOSED (0xC0000B5B)";
        result.detail = "Pseudoconsole connection was closed normally.";
        result.formatted_message = "[Process exited: pseudoconsole closed (0xC0000B5B)]";
        return result;
    }

    result.is_normal = false;

    // Check if exit code is in NTSTATUS range (0x80000000 - 0xFFFFFFFF)
    if (exit_code >= 0x80000000U)
    {
        result.is_ntstatus = true;
        switch (exit_code)
        {
        case 0xC0000005: // STATUS_ACCESS_VIOLATION
            result.summary = "STATUS_ACCESS_VIOLATION (" + result.hex_code + ")";
            result.detail = "Crash: access violation (kemungkinan komponen .NET/CLR atau console rusak pada Windows debloat).";
            break;
        case 0xC0000409: // STATUS_STACK_BUFFER_OVERRUN
            result.summary = "STATUS_STACK_BUFFER_OVERRUN (" + result.hex_code + ")";
            result.detail = "Crash: stack buffer overrun / fail-fast exception.";
            break;
        case 0xC0000135: // STATUS_DLL_NOT_FOUND
            result.summary = "STATUS_DLL_NOT_FOUND (" + result.hex_code + ")";
            result.detail = "DLL tidak ditemukan (system32 / dependency component missing).";
            break;
        case 0xC0000142: // STATUS_DLL_INIT_FAILED
            result.summary = "STATUS_DLL_INIT_FAILED (" + result.hex_code + ")";
            result.detail = "Inisialisasi DLL / console subsystem gagal.";
            break;
        case 0xC0000022: // STATUS_ACCESS_DENIED
            result.summary = "STATUS_ACCESS_DENIED (" + result.hex_code + ")";
            result.detail = "Access denied di level code-integrity (kemungkinan diblokir Smart App Control / WDAC policy).";
            break;
        case 0xC00000FD: // STATUS_STACK_OVERFLOW
            result.summary = "STATUS_STACK_OVERFLOW (" + result.hex_code + ")";
            result.detail = "Crash: stack overflow.";
            break;
        case 0xC0000374: // STATUS_HEAP_CORRUPTION
            result.summary = "STATUS_HEAP_CORRUPTION (" + result.hex_code + ")";
            result.detail = "Crash: heap corruption detected.";
            break;
        case 0xC0000017: // STATUS_NO_MEMORY
            result.summary = "STATUS_NO_MEMORY (" + result.hex_code + ")";
            result.detail = "Out of memory during process initialization.";
            break;
        default:
            result.summary = "NTSTATUS Exception (" + result.hex_code + ")";
            result.detail = "Abnormal NTSTATUS exception during process execution.";
            break;
        }

        std::ostringstream msg;
        msg << "[Process crashed with NTSTATUS " << result.hex_code << " — " << result.summary << "]";
        if (!result.detail.empty())
        {
            msg << "\n  -> " << result.detail;
        }
        if (!shell_path.empty())
        {
            msg << "\n  -> Shell: \"" << shell_path.string() << "\" ("
                << (is_conpty ? "ConPTY mode" : "Pipe mode") << ")";
        }
        msg << "\n  -> Check %TEMP%\\zde-terminal.log for full diagnostic trace.";
        result.formatted_message = msg.str();
    }
    else
    {
        result.is_ntstatus = false;
        result.summary = "Process exited with code " + std::to_string(exit_code);
        result.detail = "Non-zero exit code returned by shell command or script.";

        std::ostringstream msg;
        msg << "[Process exited with code " << exit_code << "]";
        if (!shell_path.empty())
        {
            msg << " — Shell: \"" << shell_path.string() << "\" ("
                << (is_conpty ? "ConPTY mode" : "Pipe mode") << ")";
        }
        result.formatted_message = msg.str();
    }

    return result;
}

} // namespace Zenvra::Terminal
