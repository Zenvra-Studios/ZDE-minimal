#include "Platform/PlatformWindowFactory.h"

#if defined(_WIN32)
#include "Platform/Win32/Win32Window.h"
#elif defined(__APPLE__)
#include "Platform/Cocoa/CocoaWindow.h"
#elif defined(__linux__)
#include "Platform/X11/X11Window.h"
#endif

namespace Zenvra::Platform
{

std::unique_ptr<IPlatformWindow> create_platform_window(const WindowSpecification& specification)
{
#if defined(_WIN32)
    return std::make_unique<Win32::Win32Window>(specification);
#elif defined(__APPLE__)
    return std::make_unique<Cocoa::CocoaWindow>(specification);
#elif defined(__linux__)
    return std::make_unique<X11::X11Window>(specification);
#else
    static_cast<void>(specification);
    return nullptr;
#endif
}

} // namespace Zenvra::Platform
