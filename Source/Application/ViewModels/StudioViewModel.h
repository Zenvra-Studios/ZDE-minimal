#pragma once

#include "Commands/CommandRegistry.h"

#include <functional>
#include <string_view>

namespace Zenvra::Application::ViewModels
{

struct StudioActions
{
    std::function<void()> request_close;
    std::function<void()> show_about;
    std::function<bool()> request_open_project;
};

class StudioViewModel
{
public:
    explicit StudioViewModel(StudioActions actions);

    [[nodiscard]] bool initialize();
    [[nodiscard]] Commands::CommandExecutionResult execute_command(std::string_view command_id) const;
    [[nodiscard]] const Commands::CommandRegistry& get_command_registry() const noexcept;

private:
    [[nodiscard]] bool register_available_commands();
    [[nodiscard]] bool register_future_commands();

    StudioActions m_actions;
    Commands::CommandRegistry m_command_registry;
    bool m_initialized = false;
};

} // namespace Zenvra::Application::ViewModels
