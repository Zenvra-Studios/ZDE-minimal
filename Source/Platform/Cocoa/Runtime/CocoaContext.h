#pragma once

namespace Zenvra {
namespace Platform {
namespace Cocoa {
namespace Runtime {

class CocoaContext {
public:
    static bool initialize();
    static void shutdown();
};

} // namespace Runtime
} // namespace Cocoa
} // namespace Platform
} // namespace Zenvra
