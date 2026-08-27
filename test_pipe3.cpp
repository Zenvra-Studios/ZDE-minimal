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

    CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0);
    SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&hStdInRead, &hStdInWrite, &saAttr, 0);
    SetHandleInformation(hStdInWrite, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION piProcInfo;
    STARTUPINFOW siStartInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOW));
    siStartInfo.cb = sizeof(STARTUPINFOW);
    siStartInfo.hStdError = hStdOutWrite;
    siStartInfo.hStdOutput = hStdOutWrite;
    siStartInfo.hStdInput = hStdInRead;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    std::wstring cmd = L"cmd.exe";
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo)) {
        return 1;
    }

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdInRead);

    std::string input = "echo hello\r\n";
    DWORD written;
    WriteFile(hStdInWrite, input.data(), input.size(), &written, NULL);
    Sleep(500);

    input = "a\x7F\b\b\b\b\x17";
    WriteFile(hStdInWrite, input.data(), input.size(), &written, NULL);
    Sleep(500);

    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    std::cout << "Exit code: " << exitCode << std::endl;

    TerminateProcess(piProcInfo.hProcess, 0);
    return 0;
}
