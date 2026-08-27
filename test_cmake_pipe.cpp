#include <windows.h>
#include <iostream>

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

    std::wstring cmd = L"C:\\Users\\Administrator\\scoop\\shims\\cmake.EXE --build C:/Users/Administrator/Documents/Projects/ZDE-minimal/build/windows-x64-clang-ninja-release";
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &siStartInfo, &piProcInfo)) {
        return 1;
    }

    CloseHandle(hStdOutWrite);
    CloseHandle(hStdInRead);

    Sleep(500);

    std::string input = "\x7F\b\b\b\x17";
    DWORD written;
    WriteFile(hStdInWrite, input.data(), input.size(), &written, NULL);

    WaitForSingleObject(piProcInfo.hProcess, 5000);

    DWORD exitCode = 0;
    GetExitCodeProcess(piProcInfo.hProcess, &exitCode);
    std::cout << "Exit code: " << exitCode << std::endl;

    TerminateProcess(piProcInfo.hProcess, 0);
    return 0;
}
