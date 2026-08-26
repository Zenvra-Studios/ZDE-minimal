#pragma once

namespace Zenvra::Platform::Win32::Runtime {

class WinRTContext {
public:
  static bool initialize();
  static void shutdown();

private:
  static bool s_is_initialized;
};

} // namespace Zenvra::Platform::Win32::Runtime
