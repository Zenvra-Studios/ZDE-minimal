#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Toolbar
{

enum class BuildConfigurationMode : std::uint8_t
{
    Debug,
    Release,
    RelWithDebInfo,
    MinSizeRel
};

enum class TargetArchitecture : std::uint8_t
{
    HostDefault,
    X86_64,
    X86,
    Arm64,
    Arm32,
    Universal
};

enum class ExecutionState : std::uint8_t
{
    Idle,
    Building,
    Running,
    Debugging,
    Terminated
};

enum class ToolbarActionType : std::uint8_t
{
    Build,
    Run,
    Debug,
    Stop
};

enum class ToolbarSegment : std::uint8_t
{
    Left,
    Center,
    Right
};

struct BinaryTargetProfile
{
    std::string id;
    std::string name;
    std::string executable_path;
    bool is_default = false;
};

struct RunConfigurationState
{
    std::string active_target_name = "ZDE";
    BuildConfigurationMode active_mode = BuildConfigurationMode::Debug;
    TargetArchitecture active_architecture = TargetArchitecture::HostDefault;
    std::string active_preset_name = "macos-debug";
    ExecutionState execution_state = ExecutionState::Idle;
    std::vector<BinaryTargetProfile> available_targets = {
        {"zde", "ZDE", "bin/Debug/ZDE.app/Contents/MacOS/ZDE", true},
        {"tests", "ZDEUnitTests", "bin/Debug/ZDEUnitTests", false}
    };
};

[[nodiscard]] constexpr std::string_view to_string(BuildConfigurationMode mode) noexcept
{
    switch (mode)
    {
    case BuildConfigurationMode::Debug: return "Debug";
    case BuildConfigurationMode::Release: return "Release";
    case BuildConfigurationMode::RelWithDebInfo: return "RelWithDebInfo";
    case BuildConfigurationMode::MinSizeRel: return "MinSizeRel";
    }
    return "Debug";
}

[[nodiscard]] constexpr std::string_view to_string(TargetArchitecture arch) noexcept
{
    switch (arch)
    {
    case TargetArchitecture::HostDefault: return "x86_64";
    case TargetArchitecture::X86_64: return "x86_64";
    case TargetArchitecture::X86: return "x86";
    case TargetArchitecture::Arm64: return "ARM64";
    case TargetArchitecture::Arm32: return "ARM32";
    case TargetArchitecture::Universal: return "Universal";
    }
    return "x86_64";
}

} // namespace Zenvra::UI::Toolbar
