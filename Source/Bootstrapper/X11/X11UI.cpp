#include "../NativeUI.h"
#include <iostream>

// Simplified implementation for X11/Linux, could be replaced with GTK or xmessage calls
void ShowNativeErrorDialog(const std::string& title, const std::string& message) {
    std::cerr << "ERROR: " << title << "\n" << message << std::endl;
}

void PrintDiagnostic(const std::string& message) {
    std::cout << message << std::endl;
}
