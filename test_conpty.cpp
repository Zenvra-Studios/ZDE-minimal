#include <windows.h>
#include <iostream>

typedef HRESULT(WINAPI* CreatePseudoConsole_t)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef void(WINAPI* ClosePseudoConsole_t)(HPCON);

int main() {
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    auto pCreatePseudoConsole = (CreatePseudoConsole_t)GetProcAddress(hKernel32, "CreatePseudoConsole");
    auto pClosePseudoConsole = (ClosePseudoConsole_t)GetProcAddress(hKernel32, "ClosePseudoConsole");

    if (!pCreatePseudoConsole) return 1;

    HANDLE hInRead, hInWrite, hOutRead, hOutWrite;
    CreatePipe(&hInRead, &hInWrite, NULL, 0);
    CreatePipe(&hOutRead, &hOutWrite, NULL, 0);

    HPCON hPC;
    COORD size = {80, 25};
    if (FAILED(pCreatePseudoConsole(size, hInRead, hOutWrite, 0, &hPC))) return 1;

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
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &siex.StartupInfo, &pi)) {
        return 1;
    }

    Sleep(1000);
    // Simulate conhost crash by closing the pseudoconsole abruptly
    pClosePseudoConsole(hPC);
    
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    std::cout << "PowerShell Exit Code when ConPTY closed: " << exitCode << std::endl;

    return 0;
}
