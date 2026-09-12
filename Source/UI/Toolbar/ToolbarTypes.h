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

enum class ToolClassification : std::uint8_t
{
    CMake,
    TomlCargo,
    Pascal,
    Java,
    Python,
    CustomExecutable
};

struct BinaryTargetProfile
{
    std::string id;
    std::string name;
    std::string executable_path;
    ToolClassification classification = ToolClassification::CMake;
    std::string icon_asset = "Assets/icons/material-icon-theme/cmake.svg";
    bool is_default = false;
};

struct RunConfigurationState
{
    std::string active_target_name;
    BuildConfigurationMode active_mode = BuildConfigurationMode::Debug;
    TargetArchitecture active_architecture = TargetArchitecture::HostDefault;
    ToolClassification active_classification = ToolClassification::CMake;
    std::string active_icon_asset = "Assets/icons/material-icon-theme/cmake.svg";
#if defined(__APPLE__)
    std::string active_preset_name = "macos-debug";
#elif defined(_WIN32)
    std::string active_preset_name = "windows-ninja-debug";
#else
    std::string active_preset_name = "linux-debug";
#endif
    ExecutionState execution_state = ExecutionState::Idle;
    std::vector<BinaryTargetProfile> available_targets;
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

[[nodiscard]] constexpr std::string_view to_string(ToolClassification classification) noexcept
{
    switch (classification)
    {
    case ToolClassification::CMake: return "CMake";
    case ToolClassification::TomlCargo: return "Cargo / TOML";
    case ToolClassification::Pascal: return "Pascal";
    case ToolClassification::Java: return "Java";
    case ToolClassification::Python: return "Python";
    case ToolClassification::CustomExecutable: return "Executable";
    }
    return "Tools";
}

[[nodiscard]] inline std::string get_classification_icon(ToolClassification classification)
{
    switch (classification)
    {
    case ToolClassification::CMake: return "Assets/icons/material-icon-theme/cmake.svg";
    case ToolClassification::TomlCargo: return "Assets/icons/material-icon-theme/toml.svg";
    case ToolClassification::Pascal: return "Assets/icons/material-icon-theme/pascal.svg";
    case ToolClassification::Java: return "Assets/icons/material-icon-theme/java.svg";
    case ToolClassification::Python: return "Assets/icons/material-icon-theme/python.svg";
    case ToolClassification::CustomExecutable: return "Assets/icons/terminal.svg";
    }
    return "Assets/icons/terminal.svg";
}

} // namespace Zenvra::UI::Toolbar
