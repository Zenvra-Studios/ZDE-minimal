#pragma once

namespace Zenvra {
namespace Platform {
namespace Win32 {
namespace Runtime {

class WinRTContext {
public:
    static bool initialize();
    static void shutdown();
    
private:
    static bool s_is_initialized;
};

} // namespace Runtime
} // namespace Win32
} // namespace Platform
} // namespace Zenvra
