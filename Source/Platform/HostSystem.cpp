#include "Platform/HostSystem.h"

#include <cstring>
#include <string>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/utsname.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__unix__)
#include <sys/utsname.h>
#endif

namespace Zenvra::Platform::HostSystem
{

namespace
{

Architecture query_native_architecture(std::string& out_machine_name)
{
#if defined(__APPLE__)
    char machine[256] = {0};
    std::size_t size = sizeof(machine);
    if (sysctlbyname("hw.machine", machine, &size, nullptr, 0) == 0)
    {
        out_machine_name = machine;
        std::string_view m(machine);
        if (m.starts_with("arm") || m.starts_with("aarch"))
        {
            return Architecture::Arm64;
        }
        if (m == "x86_64")
        {
            return Architecture::X86_64;
        }
    }

    struct utsname uts{};
    if (uname(&uts) == 0)
    {
        if (out_machine_name.empty())
        {
            out_machine_name = uts.machine;
        }
        std::string_view m(uts.machine);
        if (m.starts_with("arm") || m.starts_with("aarch"))
        {
            return Architecture::Arm64;
        }
        if (m == "x86_64")
        {
            return Architecture::X86_64;
        }
    }

#if defined(__arm64__) || defined(__aarch64__)
    return Architecture::Arm64;
#else
    return Architecture::X86_64;
#endif

#elif defined(_WIN32)
    SYSTEM_INFO sys_info{};
    GetNativeSystemInfo(&sys_info);
    switch (sys_info.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_ARM64:
        out_machine_name = "ARM64";
        return Architecture::Arm64;
    case PROCESSOR_ARCHITECTURE_AMD64:
        out_machine_name = "x86_64";
        return Architecture::X86_64;
    case PROCESSOR_ARCHITECTURE_INTEL:
        out_machine_name = "x86";
        return Architecture::X86;
    case PROCESSOR_ARCHITECTURE_ARM:
        out_machine_name = "ARM";
        return Architecture::Arm32;
    default:
        out_machine_name = "Unknown";
        return Architecture::Unknown;
    }

#elif defined(__linux__) || defined(__unix__)
    struct utsname uts{};
    if (uname(&uts) == 0)
    {
        out_machine_name = uts.machine;
        std::string_view m(uts.machine);
        if (m == "x86_64" || m == "amd64")
        {
            return Architecture::X86_64;
        }
        if (m == "aarch64" || m.starts_with("arm64"))
        {
            return Architecture::Arm64;
        }
        if (m == "i386" || m == "i686")
        {
            return Architecture::X86;
        }
        if (m.starts_with("arm"))
        {
            return Architecture::Arm32;
        }
    }
    out_machine_name = "x86_64";
    return Architecture::X86_64;
#else
    out_machine_name = "Generic";
    return Architecture::X86_64;
#endif
}

OperatingSystem query_operating_system()
{
#if defined(__APPLE__)
    return OperatingSystem::macOS;
#elif defined(_WIN32)
    return OperatingSystem::Windows;
#elif defined(__linux__)
    return OperatingSystem::Linux;
#else
    return OperatingSystem::Unknown;
#endif
}

SystemInfo initialize_system_info()
{
    SystemInfo info{};
    info.os = query_operating_system();
    info.arch = query_native_architecture(info.machine_name);

    switch (info.os)
    {
    case OperatingSystem::macOS:
        info.default_preset_debug = "macos-debug";
        info.default_preset_release = "macos-release";
        break;
    case OperatingSystem::Windows:
        info.default_preset_debug = "windows-x64-ninja-debug";
        info.default_preset_release = "windows-x64-ninja-release";
        break;
    case OperatingSystem::Linux:
        info.default_preset_debug = "linux-debug";
        info.default_preset_release = "linux-release";
        break;
    case OperatingSystem::Unknown:
        info.default_preset_debug = "debug";
        info.default_preset_release = "release";
        break;
    }

    return info;
}

} // namespace

const SystemInfo& get_system_info() noexcept
{
    static const SystemInfo s_info = initialize_system_info();
    return s_info;
}

Architecture get_native_architecture() noexcept
{
    return get_system_info().arch;
}

OperatingSystem get_operating_system() noexcept
{
    return get_system_info().os;
}

std::string_view to_string(Architecture arch) noexcept
{
    switch (arch)
    {
    case Architecture::X86_64: return "x86_64";
    case Architecture::Arm64: return "arm64";
    case Architecture::X86: return "x86";
    case Architecture::Arm32: return "arm32";
    case Architecture::Unknown: return "unknown";
    }
    return "unknown";
}

std::string_view to_string(OperatingSystem os) noexcept
{
    switch (os)
    {
    case OperatingSystem::macOS: return "macOS";
    case OperatingSystem::Windows: return "Windows";
    case OperatingSystem::Linux: return "Linux";
    case OperatingSystem::Unknown: return "Unknown";
    }
    return "Unknown";
}

} // namespace Zenvra::Platform::HostSystem
