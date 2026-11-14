#pragma once

#include <cstdint>
#include <string_view>

namespace Zenvra::Services::LSP::Lifecycle
{

enum class LifecycleState : std::uint8_t
{
    Uninitialized = 0,
    Discovering,
    Missing,
    Installing,
    Starting,
    Initializing,
    Running,
    Stopping,
    Crashed,
    Failed
};

[[nodiscard]] constexpr std::string_view lifecycle_state_to_string(LifecycleState state) noexcept
{
    switch (state)
    {
    case LifecycleState::Uninitialized: return "Uninitialized";
    case LifecycleState::Discovering:   return "Discovering";
    case LifecycleState::Missing:       return "Missing";
    case LifecycleState::Installing:    return "Installing";
    case LifecycleState::Starting:      return "Starting";
    case LifecycleState::Initializing:  return "Initializing";
    case LifecycleState::Running:       return "Running";
    case LifecycleState::Stopping:      return "Stopping";
    case LifecycleState::Crashed:       return "Crashed";
    case LifecycleState::Failed:        return "Failed";
    }
    return "Unknown";
}

} // namespace Zenvra::Services::LSP::Lifecycle

