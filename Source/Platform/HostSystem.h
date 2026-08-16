#pragma once

#include <string>
#include <string_view>

namespace Zenvra::Platform::HostSystem
{

enum class Architecture
{
    Unknown,
    X86_64,
    Arm64,
    X86,
    Arm32
};

enum class OperatingSystem
{
    Unknown,
    macOS,
    Windows,
    Linux
};

struct SystemInfo
{
    OperatingSystem os = OperatingSystem::Unknown;
    Architecture arch = Architecture::Unknown;
    std::string machine_name;
    std::string default_preset_debug;
    std::string default_preset_release;
};

[[nodiscard]] Architecture get_native_architecture() noexcept;
[[nodiscard]] OperatingSystem get_operating_system() noexcept;
[[nodiscard]] const SystemInfo& get_system_info() noexcept;

[[nodiscard]] std::string_view to_string(Architecture arch) noexcept;
[[nodiscard]] std::string_view to_string(OperatingSystem os) noexcept;

} // namespace Zenvra::Platform::HostSystem
