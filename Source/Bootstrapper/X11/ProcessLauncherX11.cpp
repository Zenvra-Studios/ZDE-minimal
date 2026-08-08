#include "../NativeUI.h"
#include <cstdlib>

bool LaunchProcess(const std::string& executable_path, const std::string& command_line) {
    // Basic implementation using system()
    int ret = std::system(command_line.c_str());
    return ret == 0;
}
