#pragma once

#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Runtime
{

/// Desktop notifications on Linux.
///
/// Resolution order (mirrors Win32 WinRT-toast-then-balloon fallback):
///  1. libnotify via dlopen (no hard link dependency — works whether or not
///     the -dev package was present at build time),
///  2. `notify-send` child process (present on virtually all desktops),
///  3. silent no-op returning false.
///
/// Never blocks the caller, never throws.
class DesktopNotification
{
public:
    static bool show(std::string_view title,
                     std::string_view message,
                     std::string_view tag = "");
};

} // namespace Zenvra::Platform::X11::Runtime
