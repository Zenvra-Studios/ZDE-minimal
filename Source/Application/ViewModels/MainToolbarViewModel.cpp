#include "Application/ViewModels/MainToolbarViewModel.h"
#include "Application/ViewModels/StudioViewModel.h"
#include "Commands/CommandIds.h"
#include "Platform/HostSystem.h"

namespace Zenvra::Application::ViewModels
{

MainToolbarViewModel::MainToolbarViewModel()
{
    m_toolbar.get_run_config_widget().set_active_target("Project");
    m_toolbar.get_run_config_widget().set_active_mode(UI::Toolbar::BuildConfigurationMode::Debug);
    m_toolbar.get_run_config_widget().set_active_architecture(UI::Toolbar::TargetArchitecture::HostDefault);
}

void MainToolbarViewModel::sync_with_studio(const StudioViewModel& studio_vm)
{
    set_active_target(studio_vm.get_active_target());

    if (studio_vm.get_active_mode() == "Release")
    {
        set_active_mode(UI::Toolbar::BuildConfigurationMode::Release);
    }
    else
    {
        set_active_mode(UI::Toolbar::BuildConfigurationMode::Debug);
    }

    const auto arch = studio_vm.get_active_arch();
    if (arch == "x86_64" || arch == "x64")
    {
        set_active_architecture(UI::Toolbar::TargetArchitecture::X86_64);
    }
    else if (arch == "x86" || arch == "win32")
    {
        set_active_architecture(UI::Toolbar::TargetArchitecture::X86);
    }
    else if (arch == "arm64")
    {
        set_active_architecture(UI::Toolbar::TargetArchitecture::Arm64);
    }
    else if (arch == "arm32")
    {
        set_active_architecture(UI::Toolbar::TargetArchitecture::Arm32);
    }
    else
    {
        set_active_architecture(UI::Toolbar::TargetArchitecture::HostDefault);
    }
}

void MainToolbarViewModel::set_active_target(std::string_view target_name)
{
    m_toolbar.get_run_config_widget().set_active_target(target_name);
}

void MainToolbarViewModel::set_active_mode(UI::Toolbar::BuildConfigurationMode mode)
{
    m_toolbar.get_run_config_widget().set_active_mode(mode);
}

void MainToolbarViewModel::set_active_architecture(UI::Toolbar::TargetArchitecture arch)
{
    m_toolbar.get_run_config_widget().set_active_architecture(arch);
}

void MainToolbarViewModel::set_execution_state(UI::Toolbar::ExecutionState state)
{
    m_toolbar.get_run_config_widget().set_execution_state(state);
    m_toolbar.get_action_button_group().set_execution_state(state);
}

void MainToolbarViewModel::trigger_build()
{
    if (m_invoker)
    {
        m_invoker(Commands::CommandIds::build_build_project);
    }
}

void MainToolbarViewModel::trigger_run()
{
    if (m_invoker)
    {
        m_invoker(Commands::CommandIds::run_start);
    }
}

void MainToolbarViewModel::trigger_debug()
{
    if (m_invoker)
    {
        m_invoker(Commands::CommandIds::run_debug);
    }
}

void MainToolbarViewModel::trigger_stop()
{
    if (m_invoker)
    {
        m_invoker(Commands::CommandIds::run_stop);
    }
    set_execution_state(UI::Toolbar::ExecutionState::Idle);
}

} // namespace Zenvra::Application::ViewModels
