#include "Application/ViewModels/MainToolbarViewModel.h"

#include "Commands/CommandIds.h"

namespace Zenvra::Application::ViewModels
{

MainToolbarViewModel::MainToolbarViewModel()
{
    m_toolbar.get_run_config_widget().set_active_target("ZDE");
    m_toolbar.get_run_config_widget().set_active_mode(UI::Toolbar::BuildConfigurationMode::Debug);
    m_toolbar.get_run_config_widget().set_active_architecture(UI::Toolbar::TargetArchitecture::Arm64);
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
        m_invoker(Commands::CommandIds::view_problems);
    }
}

void MainToolbarViewModel::trigger_stop()
{
    set_execution_state(UI::Toolbar::ExecutionState::Idle);
}

} // namespace Zenvra::Application::ViewModels
