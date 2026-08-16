#pragma once

#include "Commands/CommandRegistry.h"
#include "UI/Toolbar/StudioMainToolbar.h"

#include <functional>
#include <string_view>

namespace Zenvra::Application::ViewModels
{

class MainToolbarViewModel
{
public:
    MainToolbarViewModel();

    void set_command_invoker(std::function<void(std::string_view)> invoker) { m_invoker = std::move(invoker); }

    [[nodiscard]] UI::Toolbar::StudioMainToolbar& get_toolbar() noexcept { return m_toolbar; }
    [[nodiscard]] const UI::Toolbar::StudioMainToolbar& get_toolbar() const noexcept { return m_toolbar; }

    void set_active_target(std::string_view target_name);
    void set_active_mode(UI::Toolbar::BuildConfigurationMode mode);
    void set_active_architecture(UI::Toolbar::TargetArchitecture arch);
    void set_execution_state(UI::Toolbar::ExecutionState state);

    void trigger_build();
    void trigger_run();
    void trigger_debug();
    void trigger_stop();

private:
    UI::Toolbar::StudioMainToolbar m_toolbar;
    std::function<void(std::string_view)> m_invoker;
};

} // namespace Zenvra::Application::ViewModels
