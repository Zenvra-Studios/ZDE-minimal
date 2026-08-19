#include "WinRTContext.h"
#include <windows.h>
#include <iostream>

// Menggunakan modern C++/WinRT headers bawaan Windows SDK (Visual Studio)
#include <winrt/Windows.Foundation.h>

namespace Zenvra::Platform::Win32::Runtime {

bool WinRTContext::s_is_initialized = false;

bool WinRTContext::initialize()
{
    if (s_is_initialized) {
        return true;
    }

    try {
        // Initialize WinRT menggunakan C++/WinRT
        // default parameter untuk winrt::init_apartment() adalah winrt::apartment_type::multi_threaded
        winrt::init_apartment();
        
        s_is_initialized = true;
        return true;
    } catch (const winrt::hresult_error& e) {
        // Fallback jika sudah terinisialisasi dengan concurrency model berbeda, dsb.
        std::wcerr << L"WinRT initialization warning/error: " << e.message().c_str() << std::endl;
        
        // Kita bisa anggap true jika error code spesifik RPC_E_CHANGED_MODE, 
        // tapi C++/WinRT akan melempar error ini sebagai exception
        if (e.code() == RPC_E_CHANGED_MODE) {
            s_is_initialized = true;
            return true;
        }
        return false;
    }
}

void WinRTContext::shutdown()
{
    if (s_is_initialized) {
        winrt::uninit_apartment();
        s_is_initialized = false;
    }
}

} // namespace Zenvra::Platform::Win32::Runtime
