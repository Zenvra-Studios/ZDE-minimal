#include "WinRTContext.h"
#include "Platform/Win32/Utility/Win32Compat.h"
#include <iostream>
#include <windows.h>

#if __has_include(<winrt/Windows.Foundation.h>)
#include <winrt/Windows.Foundation.h>
#define ZDE_HAS_WINRT 1
#endif

namespace Zenvra::Platform::Win32::Runtime {

bool WinRTContext::s_is_initialized = false;

bool WinRTContext::initialize() {
  if (s_is_initialized) {
    return true;
  }

#if defined(ZDE_HAS_WINRT)
  // WinRT lives in combase.dll, which only exists on Windows 8+. The combase
  // import is delay-loaded, so reaching this early-return on Windows 7 keeps
  // the process alive with WinRT features disabled instead of aborting the
  // loader or raising a delay-load failure.
  if (!Compat::is_windows_8_or_greater()) {
    return false;
  }
  try {
    winrt::init_apartment();
    s_is_initialized = true;
    return true;
  } catch (const winrt::hresult_error &e) {
    std::wcerr << L"WinRT initialization warning/error: " << e.message().c_str()
               << std::endl;
    if (e.code() == RPC_E_CHANGED_MODE) {
      s_is_initialized = true;
      return true;
    }
    return false;
  }
#else
  s_is_initialized = false;
  return false;
#endif
}

void WinRTContext::shutdown() {
#if defined(ZDE_HAS_WINRT)
  if (s_is_initialized) {
    winrt::uninit_apartment();
    s_is_initialized = false;
  }
#endif
}

} // namespace Zenvra::Platform::Win32::Runtime
