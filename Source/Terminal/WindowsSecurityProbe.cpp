#include "WindowsSecurityProbe.h"

#include <mutex>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Zenvra::Terminal
{

namespace
{
#if defined(_WIN32)
std::string read_reg_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name)
{
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
    {
        return "";
    }
    std::wstring result;
    DWORD type = 0;
    DWORD byte_size = 0;
    if (RegQueryValueExW(hkey, value_name, nullptr, &type, nullptr, &byte_size) == ERROR_SUCCESS && byte_size > 0)
    {
        result.resize(byte_size / sizeof(wchar_t));
        if (RegQueryValueExW(hkey, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(result.data()), &byte_size) == ERROR_SUCCESS)
        {
            while (!result.empty() && result.back() == L'\0')
            {
                result.pop_back();
            }
        }
        else
        {
            result.clear();
        }
    }
    RegCloseKey(hkey);

    if (result.empty())
    {
        return "";
    }
    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, result.c_str(), static_cast<int>(result.size()), nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0)
    {
        return "";
    }
    std::string utf8_result(utf8_len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, result.c_str(), static_cast<int>(result.size()), utf8_result.data(), utf8_len, nullptr, nullptr);
    return utf8_result;
}

DWORD read_reg_dword(HKEY root, const wchar_t* subkey, const wchar_t* value_name, DWORD default_val = static_cast<DWORD>(-1))
{
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
    {
        return default_val;
    }
    DWORD val = default_val;
    DWORD type = 0;
    DWORD size = sizeof(DWORD);
    if (RegQueryValueExW(hkey, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) != ERROR_SUCCESS)
    {
        val = default_val;
    }
    RegCloseKey(hkey);
    return val;
}
#endif
} // namespace

const WindowsSecurityInfo& WindowsSecurityProbe::get_cached_info() noexcept
{
    static std::once_flag s_once;
    static WindowsSecurityInfo s_cached;
    std::call_once(s_once, []() {
        s_cached = probe_system();
    });
    return s_cached;
}

WindowsSecurityInfo WindowsSecurityProbe::probe_system()
{
    WindowsSecurityInfo info;
#if defined(_WIN32)
    // 1. Read OS Details
    info.product_name = read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    info.display_version = read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    info.build_number = read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    info.ubr = read_reg_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", 0);

    // 2. Read SAC (Smart App Control) policy state
    const DWORD sac_val = read_reg_dword(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI\\Policy", L"VerifiedAndReputablePolicyState", static_cast<DWORD>(-1));
    if (sac_val == 0)
    {
        info.sac_state = SACState::Disabled;
    }
    else if (sac_val == 1)
    {
        info.sac_state = SACState::Evaluation;
    }
    else if (sac_val == 2)
    {
        info.sac_state = SACState::Enforced;
    }
    else
    {
        info.sac_state = SACState::Unknown;
    }

    // 3. Check Windows Defender service presence via SCM
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm != nullptr)
    {
        SC_HANDLE svc = OpenServiceW(scm, L"WinDefend", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
        if (svc != nullptr)
        {
            info.defender_status = DefenderServiceStatus::Present;
            CloseServiceHandle(svc);
        }
        else
        {
            const DWORD err = GetLastError();
            if (err == ERROR_SERVICE_DOES_NOT_EXIST)
            {
                info.defender_status = DefenderServiceStatus::MissingOrStripped;
            }
            else
            {
                info.defender_status = DefenderServiceStatus::Present; // Access denied or stopped means service exists
            }
        }
        CloseServiceHandle(scm);
    }

    // 4. Check Franken-mod (Win11 build but ProductName says Windows 10)
    uint32_t build_int = 0;
    if (!info.build_number.empty())
    {
        try { build_int = static_cast<uint32_t>(std::stoul(info.build_number)); } catch (...) {}
    }
    if (build_int >= 22000 && info.product_name.find("Windows 10") != std::string::npos)
    {
        info.is_franken_mod = true;
    }

    // 5. Check .NET Framework Health (Full Release DWORD)
    const DWORD release_val = read_reg_dword(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full", L"Release", 0);
    info.dotnet_release = release_val;
    info.dotnet_installed = (release_val >= 528040); // 528040 is .NET Framework 4.8

    // 6. Classification
    info.classification = SystemHealthClassification::Healthy;
    if (info.defender_status == DefenderServiceStatus::MissingOrStripped)
    {
        info.classification = SystemHealthClassification::Debloated;
        info.classification_reasons.push_back("Windows Defender service (WinDefend) has been removed / stripped.");
    }
    if (info.is_franken_mod)
    {
        info.classification = SystemHealthClassification::Debloated;
        info.classification_reasons.push_back("Franken-mod detected: Windows 11 kernel with Windows 10 ProductName metadata.");
    }
    if (!info.dotnet_installed)
    {
        info.classification = SystemHealthClassification::Broken;
        info.classification_reasons.push_back(".NET Framework 4.8+ is missing or registry is damaged.");
    }
    if (info.sac_state == SACState::Enforced && info.defender_status == DefenderServiceStatus::MissingOrStripped)
    {
        info.classification = SystemHealthClassification::Debloated;
        info.classification_reasons.push_back("Smart App Control (SAC) is ENFORCED on a debloated system lacking Defender verdict infrastructure.");
    }
#endif
    return info;
}

std::string WindowsSecurityProbe::generate_troubleshooting_hint(const TerminalExitDecoded& exit_info)
{
    const auto& probe = get_cached_info();
    std::ostringstream hint;

    // SAC friction hint
    if (probe.sac_state == SACState::Enforced || probe.sac_state == SACState::Evaluation)
    {
        hint << "\n[ZDE Security Hint] Smart App Control terdeteksi AKTIF ("
             << sac_state_to_string(probe.sac_state) << ").\n"
             << "  Pada Windows mod/debloat tanpa Defender, SAC dapat membuat inisialisasi shell gagal.\n"
             << "  Untuk menonaktifkan SAC via command prompt administrator:\n"
             << "    reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\CI\\Policy\" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 0 /f\n"
             << "  lalu restart PC.\n";
    }

    // CLR / Access Violation or missing DLL hint
    if (exit_info.exit_code == 0xC0000005 || exit_info.exit_code == 0xC0000135 || !probe.dotnet_installed)
    {
        hint << "\n[ZDE Debloat Hint] Crash CLR/.NET terdeteksi (" << exit_info.hex_code << ").\n"
             << "  Komponen .NET Framework atau PowerShell 5.1 sistem mungkin rusak.\n"
             << "  Saran pemulihan:\n"
             << "    1. Gunakan Command Prompt: atur environment variable ZDE_SHELL=C:\\Windows\\System32\\cmd.exe\n"
             << "    2. Atau instal PowerShell 7 (pwsh.exe) resmi dari Microsoft\n"
             << "    3. Jalankan 'sfc /scannow' di Terminal Admin untuk perbaikan komponen sistem.\n";
    }

    return hint.str();
}

std::string WindowsSecurityProbe::sac_state_to_string(SACState state)
{
    switch (state)
    {
    case SACState::Disabled: return "OFF (Disabled)";
    case SACState::Evaluation: return "EVALUATION (Audit)";
    case SACState::Enforced: return "ON (Enforced)";
    case SACState::Unknown: default: return "Unknown / Not Applicable";
    }
}

std::string WindowsSecurityProbe::classification_to_string(SystemHealthClassification classification)
{
    switch (classification)
    {
    case SystemHealthClassification::Healthy: return "HEALTHY";
    case SystemHealthClassification::Debloated: return "DEBLOATED (Mod/Stripped)";
    case SystemHealthClassification::Broken: return "BROKEN (Components Missing)";
    default: return "UNKNOWN";
    }
}

} // namespace Zenvra::Terminal
