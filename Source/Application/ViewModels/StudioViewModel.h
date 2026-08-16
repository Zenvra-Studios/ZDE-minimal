#pragma once

#include "Commands/CommandRegistry.h"
#include "Platform/HostSystem.h"
#include "UI/Toolbar/ToolbarTypes.h"

#include <functional>
#include <string_view>

namespace Zenvra::Application::ViewModels
{

struct StudioActions
{
    std::function<void()> request_close;
    std::function<void()> show_about;
    std::function<bool()> request_open_project;
    std::function<bool()> request_close_project;
    std::function<void()> request_new_window;
    std::function<void()> request_open_folder;
    std::function<void()> request_open_recent;
    std::function<void()> request_open_remote;
    std::function<void()> request_add_folder_to_project;
    std::function<void()> request_save_as;
    std::function<void()> request_save_all;
    std::function<void()> request_close_window;
    std::function<void()> request_toggle_terminal;
    std::function<void()> request_toggle_fullscreen;
    std::function<void()> request_toggle_shader;
    std::function<void()> request_build;
    std::function<void()> request_run;
    std::function<void()> request_debug;
    std::function<void()> request_stop;
};

class StudioViewModel
{
public:
    explicit StudioViewModel(StudioActions actions);

    [[nodiscard]] bool initialize();
    [[nodiscard]] Commands::CommandExecutionResult execute_command(std::string_view command_id) const;
    [[nodiscard]] const Commands::CommandRegistry& get_command_registry() const noexcept;

    void set_active_target(std::string_view target) { m_active_target = std::string(target); }
    void set_active_mode(std::string_view mode) { m_active_mode = std::string(mode); }
    void set_active_arch(std::string_view arch) { m_active_arch = std::string(arch); }
    void set_active_preset(std::string_view preset) { m_active_preset = std::string(preset); }

    [[nodiscard]] std::string_view get_active_target() const noexcept { return m_active_target; }
    [[nodiscard]] std::string_view get_active_mode() const noexcept { return m_active_mode; }
    [[nodiscard]] std::string_view get_active_arch() const noexcept { return m_active_arch; }
    [[nodiscard]] std::string_view get_active_preset() const noexcept { return m_active_preset; }

private:
    [[nodiscard]] bool register_available_commands();
    [[nodiscard]] bool register_future_commands();

    StudioActions m_actions;
    Commands::CommandRegistry m_command_registry;
    std::string m_active_target = "ZDE";
    std::string m_active_mode = "Debug";
    std::string m_active_arch = std::string(Platform::HostSystem::to_string(Platform::HostSystem::get_native_architecture()));
    std::string m_active_preset = Platform::HostSystem::get_system_info().default_preset_debug;
    bool m_initialized = false;
};

} // namespace Zenvra::Application::ViewModels
