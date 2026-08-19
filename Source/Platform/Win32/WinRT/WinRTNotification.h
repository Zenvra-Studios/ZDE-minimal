#pragma once

#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Runtime {

class WinRTNotification {
public:
    static bool show_toast(std::wstring_view title, std::wstring_view message, std::wstring_view tag = L"");
};

} // namespace Zenvra::Platform::Win32::Runtime
