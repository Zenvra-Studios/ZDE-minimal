#include "Application/Application.h"
#include "Platform/PlatformWindowFactory.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace Zenvra::Application
{

Application::Application(ApplicationSpecification specification)
    : m_specification(std::move(specification))
{
}

Application::~Application() = default;

int Application::run()
{
    if (!initialize())
    {
        return 1;
    }

    m_window->show();
    std::uint32_t smoke_iteration_count = 0;
    while (!m_window->should_close())
    {
        m_window->poll_events();

        if (m_specification.smoke_test && ++smoke_iteration_count >= 3)
        {
            m_window->request_close();
        }

        // The renderer will own frame pacing once Phase 3 is implemented.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

void Application::request_close()
{
    if (m_window)
    {
        m_window->request_close();
    }
}

Platform::IPlatformWindow* Application::get_window() const noexcept
{
    return m_window.get();
}

const Commands::CommandRegistry* Application::get_commands() const noexcept
{
    return m_studio_view_model ? &m_studio_view_model->get_command_registry() : nullptr;
}

bool Application::initialize()
{
    Platform::WindowSpecification window_specification{
        .title = m_specification.name,
        .width = m_specification.width,
        .height = m_specification.height,
        .custom_chrome_enabled = m_specification.custom_titlebar,
    };

    m_window = Platform::create_platform_window(window_specification);
    if (!m_window)
    {
        std::cerr << "Fatal error: this platform does not have a ZDE window backend.\n";
        return false;
    }

    m_studio_view_model = std::make_unique<ViewModels::StudioViewModel>(ViewModels::StudioActions{
        .request_close = [this] { request_close(); },
        .show_about = [this] { show_about(); },
        .request_open_project = [this] { return m_window->open_project_folder(); },
        .request_new_window = [] {
            std::clog << "[ZDE] New Window requested (not yet implemented)\n";
        },
        .request_open_folder = [this] {
            m_window->open_project_folder();
        },
        .request_open_recent = [] {
            std::clog << "[ZDE] Open Recent requested (not yet implemented)\n";
        },
        .request_open_remote = [] {
            std::clog << "[ZDE] Open Remote requested (not yet implemented)\n";
        },
        .request_add_folder_to_project = [] {
            std::clog << "[ZDE] Add Folder to Project requested (not yet implemented)\n";
        },
        .request_save_as = [] {
            std::clog << "[ZDE] Save As requested (not yet implemented)\n";
        },
        .request_save_all = [] {
            std::clog << "[ZDE] Save All requested (not yet implemented)\n";
        },
        .request_close_window = [this] { request_close(); },
        .request_toggle_terminal = [this] { m_window->toggle_terminal(); },
    });

    if (!m_studio_view_model->initialize())
    {
        std::cerr << "Fatal error: the Studio command model could not be initialized.\n";
        return false;
    }

    m_window->set_command_invoked_callback([this](std::string_view command_id) {
        const Commands::CommandExecutionResult result = m_studio_view_model->execute_command(command_id);
        if (result != Commands::CommandExecutionResult::Executed)
        {
            std::clog << "Command was not executed: " << command_id << '\n';
        }
    });
    m_window->set_command_state_query_callback([this](std::string_view command_id) {
        const Commands::CommandRegistry& registry = m_studio_view_model->get_command_registry();
        return Platform::CommandPresentationState{
            .enabled = registry.is_command_enabled(command_id),
            .checked = registry.is_command_checked(command_id),
        };
    });

    if (!m_window->initialize())
    {
        std::cerr << "Fatal error: the platform window could not be initialized.\n";
        return false;
    }

    return true;
}

void Application::show_about() const
{
    std::cout << m_specification.name << " v0.1.0\n"
              << "ZDE-owned application foundation (MVVM).\n";
}

} // namespace Zenvra::Application
