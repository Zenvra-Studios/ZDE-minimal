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
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    siStartInfo.wShowWindow = SW_HIDE;
    siStartInfo.hStdError = hStdOutWrite;
    siStartInfo.hStdOutput = hStdOutWrite;
    siStartInfo.hStdInput = hStdInRead;

    std::wstring cmd = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring dir = L"C:\\Users\\Administrator\\Documents\\Projects\\ZDE-minimal\\build\\windows-x64-clang-ninja-release\\bin\\Release";
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, &dir[0], &siStartInfo, &piProcInfo)) {
        std::cout << "Failed to start process" << std::endl;
        return 1;
    }

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdInRead);

    Sleep(1000);
    
    // Simulate typing and backspaces
    std::string input = "a\b";
    DWORD written;
    for (int i = 0; i < 100; i++) {
        WriteFile(hStdInWrite, input.data(), input.size(), &written, NULL);
        Sleep(10);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    std::cout << "Exit code after backspaces: " << exitCode << std::endl;

    TerminateProcess(piProcInfo.hProcess, 0);
    return 0;
}
