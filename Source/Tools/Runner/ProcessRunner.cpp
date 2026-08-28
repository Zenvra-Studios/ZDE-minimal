#include "Tools/Runner/ProcessRunner.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#if defined(_WIN32)
#include <windows.h>
#include <vector>
#endif

namespace Zenvra::Tools::Runner
{

bool ProcessRunner::launch_process(
    const ProcessExecutionOptions& options,
    std::function<void(std::string_view)> /*stdout_callback*/) const
{
    if (!std::filesystem::exists(options.executable_path))
    {
        return false;
    }

    std::ostringstream cmd;
    cmd << "\"" << options.executable_path.string() << "\"";
    for (const auto& arg : options.arguments)
    {
        cmd << " " << arg;
    }

    if (options.run_in_background)
    {
#if defined(_WIN32)
        // FIX bentrok CMD vs PowerShell: " &" di cmd = separator, di PS 5.1 = call operator,
        // bukan background. Di Linux baru "&" = background. Di Windows harus CreateProcess DETACHED.
        const std::string cmd_str = cmd.str();
        std::wstring wcmd(cmd_str.begin(), cmd_str.end());
        std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                       DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (ok)
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return true;
        }
        return false;
#else
        cmd << " &";
        const int res = std::system(cmd.str().c_str());
        return (res == 0);
#endif
    }

    const int res = std::system(cmd.str().c_str());
    return (res == 0);
}

void ProcessRunner::terminate_active_process() const
{
    // No-op or kill signal
}

} // namespace Zenvra::Tools::Runner
