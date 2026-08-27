#include <windows.h>
#include <iostream>

typedef HRESULT(WINAPI* CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef void(WINAPI* ClosePseudoConsole_t)(HPCON);

int main() {
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    auto pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(hKernel32, "CreatePseudoConsole");
    auto pClosePseudoConsole = (ClosePseudoConsole_t)GetProcAddress(hKernel32, "ClosePseudoConsole");

    HANDLE hInRead, hInWrite, hOutRead, hOutWrite;
    CreatePipe(&hInRead, &hInWrite, NULL, 0);
    CreatePipe(&hOutRead, &hOutWrite, NULL, 0);

    HPCON hPC;
    COORD size = {80, 25};
    pCreatePseudoConsole(size, hInRead, hOutWrite, 0, &hPC);

    STARTUPINFOEXW siex;
    ZeroMemory(&siex, sizeof(siex));
    siex.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);
    siex.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attrListSize);
    InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0, &attrListSize);
    UpdateProcThreadAttribute(siex.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), NULL, NULL);

    PROCESS_INFORMATION pi;
    std::wstring cmd = L"powershell.exe -NoLogo";
    CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &siex.StartupInfo, &pi);

    std::string input = "echo hello\r\n";
    DWORD written;
    WriteFile(hInWrite, input.data(), input.size(), &written, NULL);
    Sleep(500);

    input = "some text";
    WriteFile(hInWrite, input.data(), input.size(), &written, NULL);
    Sleep(100);

    // Send lots of backspaces and deletes
    input = "\x7F\x7F\x7F\b\b\b\x1B[3~\x1B[3~";
    for(int i=0; i<10; ++i) {
        WriteFile(hInWrite, input.data(), input.size(), &written, NULL);
    }
    Sleep(1000);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    std::cout << "PowerShell Exit Code: " << exitCode << std::endl;

    pClosePseudoConsole(hPC);
    TerminateProcess(pi.hProcess, 0);
    return 0;
}
