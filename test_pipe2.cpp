#include <windows.h>
#include <iostream>
#include <string>

int main() {
    HANDLE hStdInRead, hStdInWrite;
    HANDLE hStdOutRead, hStdOutWrite;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) return 1;
    if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) return 1;

    if (!CreatePipe(&hStdInRead, &hStdInWrite, &saAttr, 0)) return 1;
    if (!SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0)) return 1;

    PROCESS_INFORMATION piProcInfo;
    STARTUPINFOW siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOW));
    siStartInfo.cb = sizeof(STARTUPINFOW);
    siStartInfo.hStdError = hStdOutWrite;
    siStartInfo.hStdOutput = hStdOutWrite;
    siStartInfo.hStdInput = hStdInRead;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::wstring cmd = L"powershell.exe -NoLogo";
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo)) {
        return 1;
    }

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdInRead);

    std::string input = "echo hello\r\n\x7F\b\x17\x03";
    DWORD written;
    WriteFile(hStdInWrite, input.data(), input.size(), &written, NULL);

    Sleep(500);

    // Close stdin pipe while powershell is reading!
    CloseHandle(hStdInWrite);

    WaitForSingleObject(piProcInfo.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    std::cout << "Exit code: " << exitCode << std::endl;

    CloseHandle(piProcInfo.hProcess);
    CloseHandle(piProcInfo.hThread);
    CloseHandle(hStdOutRead);

    return 0;
}
