#include "../NativeUI.h"
#include <windows.h>

void ShowNativeErrorDialog(const std::string& title, const std::string& message) {
    std::wstring wtitle(title.begin(), title.end());
    std::wstring wmessage(message.begin(), message.end());
    
    MessageBoxW(NULL, wmessage.c_str(), wtitle.c_str(), MB_OK | MB_ICONERROR | MB_TOPMOST);
}

#include <iostream>
void PrintDiagnostic(const std::string& message) {
    std::cout << message << std::endl;
}
