#include "../NativeUI.h"
#include <windows.h>
#include <vector>

bool LaunchProcess(const std::string& executable_path, const std::string& command_line) {
    std::wstring wexec(executable_path.begin(), executable_path.end());
    std::wstring wcmd(command_line.begin(), command_line.end());
    
    std::vector<wchar_t> cmd_buffer(wcmd.begin(), wcmd.end());
    cmd_buffer.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    std::wstring dir = wexec.substr(0, wexec.find_last_of(L"\\/"));

    bool success = CreateProcessW(
        wexec.c_str(),
        cmd_buffer.data(),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        dir.empty() ? NULL : dir.c_str(),
        &si,
        &pi
    );

    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}
