#include <iostream>

// Include platform-specific headers based on the active platform
#if defined(_WIN32) || defined(_WIN64)
#include "Platform/Win32/Win32Window.h"
// C++/WinRT only ships with the MSVC toolchain (not MinGW / standalone Clang)
#if defined(_MSC_VER)
#include "Platform/Win32/WinRT/WinRTContext.h"
#endif
#elif defined(__linux__)
#include "Platform/X11/X11Window.h"
#include "Platform/X11/Runtime/X11Context.h"
#elif defined(__APPLE__)
#include "Platform/Cocoa/CocoaWindow.h"
#include "Platform/Cocoa/Runtime/CocoaContext.h"
#else
// Add Unix/Linux specific headers here later
// #include "Platform/Unix/UnixWindow.h" 
#include <unistd.h>
#endif

// Universal Entry Point
int main(int argc, char** argv)
{
    std::cout << "Initializing Zenvra Development Studio Kernel..." << std::endl;

#if defined(_WIN32) || defined(_WIN64)
    using namespace Zenvra::Platform::Win32;

#if defined(_MSC_VER)
    if (!Runtime::WinRTContext::initialize()) {
        std::cerr << "Warning: Failed to initialize WinRT subsystem." << std::endl;
    }
#endif

    Win32Window app_window(L"Zenvra Development Studio", 1280, 720);
    
    if (!app_window.initialize()) {
        std::cerr << "Fatal Error: Failed to initialize the Win32 window subsystem!" << std::endl;
        return -1;
    }

    app_window.show();

    // Universal Main Loop
    while (!app_window.should_close()) {
        app_window.update(); // This handles platform-specific events (like Win32 message loop)
        
        // Universal Application/Engine Logic and Render steps can go here
        // ...
    }

#elif defined(__linux__)
    using namespace Zenvra::Platform::X11;

    if (!Runtime::X11Context::initialize()) {
        std::cerr << "Warning: Failed to initialize X11 Runtime Context." << std::endl;
    }

    X11Window app_window("Zenvra Development Studio", 1280, 720);
    
    if (!app_window.initialize()) {
        std::cerr << "Fatal Error: Failed to initialize the X11 window subsystem!" << std::endl;
        return -1;
    }

    app_window.show();

    // Universal Main Loop
    while (!app_window.should_close()) {
        app_window.update(); // This handles platform-specific events (like X11 message loop)
        
        // Universal Application/Engine Logic and Render steps can go here
        // ...
    }

#elif defined(__APPLE__)
    using namespace Zenvra::Platform::Cocoa;

    if (!Runtime::CocoaContext::initialize()) {
        std::cerr << "Warning: Failed to initialize Cocoa Runtime Context." << std::endl;
    }

    CocoaWindow app_window("Zenvra Development Studio", 1280, 720);
    
    if (!app_window.initialize()) {
        std::cerr << "Fatal Error: Failed to initialize the Cocoa window subsystem!" << std::endl;
        return -1;
    }

    app_window.show();

    // Universal Main Loop
    while (!app_window.should_close()) {
        app_window.update(); // This handles Cocoa NSRunLoop manual pumping
        
        // Universal Application/Engine Logic and Render steps can go here
        // ...
    }

#else
    std::cerr << "Fatal Error: Selected platform is not yet supported!" << std::endl;
    return -1;
#endif

#if defined(_WIN32) || defined(_WIN64)
#if defined(_MSC_VER)
    Zenvra::Platform::Win32::Runtime::WinRTContext::shutdown();
#endif
#elif defined(__linux__)
    Zenvra::Platform::X11::Runtime::X11Context::shutdown();
#elif defined(__APPLE__)
    Zenvra::Platform::Cocoa::Runtime::CocoaContext::shutdown();
#endif

    std::cout << "Shutting down gracefully." << std::endl;
    return 0;
}
