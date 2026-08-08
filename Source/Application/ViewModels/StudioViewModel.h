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
    std::function<void()> request_new_window;
    std::function<void()> request_open_folder;
    std::function<void()> request_open_recent;
    std::function<void()> request_open_remote;
    std::function<void()> request_add_folder_to_project;
    std::function<void()> request_save_as;
    std::function<void()> request_save_all;
    std::function<void()> request_close_window;
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
