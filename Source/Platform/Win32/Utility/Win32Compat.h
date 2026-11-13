#pragma once

// Win32 version-compatibility shims.
//
// ZDE targets Windows 7 at runtime but is built against a modern Windows
// SDK. Several user32 entry points used by the window layer only exist on
// Windows 8.1+ / Windows 10 1607+. Linking them directly would place them in
// the import table and abort the loader on Windows 7 ("entry point not
// found") before any code runs.
//
// Every call below is therefore resolved dynamically with GetProcAddress and
// falls back to the closest classic behaviour when the OS does not provide
// it. This mirrors how the terminal layer loads ConPTY dynamically
// (see Source/Terminal/ConPTYBackend.cpp).
//
// NOTE: never call the raw SDK functions from platform code; use these
// wrappers so Windows 7 keeps booting.

#include <windows.h>

namespace Zenvra::Platform::Win32::Compat {

namespace detail {

inline HMODULE user32() noexcept {
    return GetModuleHandleW(L"user32.dll");
}

} // namespace detail

/// True on Windows 8 / Server 2012 or newer. Uses ntdll!RtlGetVersion so the
/// result is unaffected by compatibility-shim version lies. WinRT (which
/// lives in combase.dll, Windows 8+) must be gated behind this check.
inline bool is_windows_8_or_greater() noexcept {
    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        return false; // Cannot tell; assume the older OS.
    }
    const auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(ntdll, "RtlGetVersion"));
    if (rtl_get_version == nullptr) {
        return false;
    }

    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtl_get_version(&info) != 0) {
        return false;
    }
    return info.dwMajorVersion > 6 ||
           (info.dwMajorVersion == 6 && info.dwMinorVersion >= 2);
}

/// Requests Per-Monitor V2 process DPI awareness (Windows 10 1607+), falling
/// back to shcore!SetProcessDpiAwareness (Windows 8.1+) and finally
/// user32!SetProcessDPIAware (Vista+). The application manifest already
/// declares the same preference, so this is belt-and-braces for processes
/// started without the manifest applied.
inline void set_process_dpi_awareness_per_monitor_v2() noexcept {
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (DPI_AWARENESS_CONTEXT)-4.
    // Declared locally so the header stays independent of SDK WINVER guards.
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(HANDLE);
    const HANDLE per_monitor_v2 =
        reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));

    if (const HMODULE user32 = detail::user32()) {
        const auto set_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_context != nullptr &&
            set_context(per_monitor_v2) != FALSE) {
            return;
        }
    }

    // Windows 8.1 fallback: shcore!SetProcessDpiAwareness(2) maps to
    // PROCESS_PER_MONITOR_DPI_AWARE. Missing DLL simply means Windows 7.
    if (const HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI *)(int);
        const auto set_awareness = reinterpret_cast<SetProcessDpiAwarenessFn>(
            GetProcAddress(shcore, "SetProcessDpiAwareness"));
        if (set_awareness != nullptr && set_awareness(2) == S_OK) {
            return;
        }
    }

    // Vista/7 fallback: system DPI awareness only.
    SetProcessDPIAware();
}

/// Window DPI (Windows 10 1607+). Falls back to the system DPI exposed by
/// GetDeviceCaps(LOGPIXELSX), which is the only notion of DPI Windows 7 has
/// (there is no per-monitor awareness there).
inline UINT get_dpi_for_window(HWND window) noexcept {
    if (const HMODULE user32 = detail::user32()) {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        const auto get_dpi = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (get_dpi != nullptr) {
            return get_dpi(window);
        }
    }

    const HDC dc = GetDC(window);
    const UINT dpi =
        dc != nullptr ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96u;
    if (dc != nullptr) {
        ReleaseDC(window, dc);
    }
    return dpi != 0u ? dpi : 96u;
}

/// DPI-scaled system metric (Windows 10 1607+). On Windows 7 the system
/// metrics are already scaled by the (single, system-wide) DPI, so plain
/// GetSystemMetrics is the correct fallback.
inline int get_system_metrics_for_dpi(int index, UINT dpi) noexcept {
    if (const HMODULE user32 = detail::user32()) {
        using GetSystemMetricsForDpiFn = int(WINAPI *)(int, UINT);
        const auto get_metrics = reinterpret_cast<GetSystemMetricsForDpiFn>(
            GetProcAddress(user32, "GetSystemMetricsForDpi"));
        if (get_metrics != nullptr) {
            return get_metrics(index, dpi);
        }
    }
    (void)dpi;
    return GetSystemMetrics(index);
}

/// Disables Windows ghosting (Windows 8.1+). Older systems have no public
/// API for this, so it is a safe no-op there.
inline void disable_process_windows_ghosting() noexcept {
    if (const HMODULE user32 = detail::user32()) {
        using DisableProcessWindowsGhostingFn = void(WINAPI *)();
        const auto disable = reinterpret_cast<DisableProcessWindowsGhostingFn>(
            GetProcAddress(user32, "DisableProcessWindowsGhosting"));
        if (disable != nullptr) {
            disable();
        }
    }
}

} // namespace Zenvra::Platform::Win32::Compat
