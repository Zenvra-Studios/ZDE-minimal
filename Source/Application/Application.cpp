#include "Application/Application.h"
#include "Language/LanguageServerManager.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Platform/PlatformWindowFactory.h"
#include "Utility/MultiContext.h"

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

    std::uint32_t smoke_iteration_count = 0;
    while (!m_windows.empty())
    {
        // Poll events for each active window context
        for (std::size_t i = 0; i < m_windows.size(); ++i)
        {
            if (m_windows[i] && m_windows[i]->window)
            {
                m_windows[i]->window->poll_events();
            }
        }

        // Clean up closed windows and unregister from MultiContextManager
        std::erase_if(m_windows, [](const std::unique_ptr<WindowContext>& ctx) {
            if (!ctx || !ctx->window || ctx->window->should_close())
            {
                if (ctx && ctx->window)
                {
                    Utility::MultiContextManager::instance().unregister_by_window(ctx->window.get());
                }
                return true;
            }
            return false;
        });

        if (m_specification.smoke_test && ++smoke_iteration_count >= 3)
        {
            request_close();
        }

        // Frame pacing
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

void Application::request_close()
{
    for (auto& ctx : m_windows)
    {
        if (ctx && ctx->window)
        {
            ctx->window->request_close();
        }
    }
}

void Application::close_window(Platform::IPlatformWindow* window)
{
    if (window != nullptr)
    {
        window->request_close();
    }
}

std::size_t Application::get_window_count() const noexcept
{
    return m_windows.size();
}

Platform::IPlatformWindow* Application::get_window() const noexcept
{
    return m_windows.empty() ? nullptr : m_windows.front()->window.get();
}

const Commands::CommandRegistry* Application::get_commands() const noexcept
{
    return m_windows.empty() ? nullptr : &m_windows.front()->studio_view_model->get_command_registry();
}

Platform::IPlatformWindow* Application::create_new_window(
    std::optional<std::filesystem::path> initial_path)
{
    Platform::WindowSpecification window_specification{
        .title = m_specification.name,
        .width = m_specification.width,
        .height = m_specification.height,
        .custom_chrome_enabled = m_specification.custom_titlebar,
    };

    auto platform_window = Platform::create_platform_window(window_specification);
    if (!platform_window)
    {
        std::cerr << "Fatal error: this platform does not have a ZDE window backend.\n";
        return nullptr;
    }

    auto* win_ptr = platform_window.get();

    auto view_model = std::make_unique<ViewModels::StudioViewModel>(ViewModels::StudioActions{
        .request_close = [this, win_ptr] { close_window(win_ptr); },
        .show_about = [this, win_ptr] { show_about(win_ptr); },
        .request_open_project = [win_ptr] { return win_ptr->open_project_folder(); },
        .request_new_window = [this] {
            static_cast<void>(create_new_window());
        },
        .request_open_folder = [win_ptr] {
            win_ptr->open_project_folder();
        },
        .request_open_recent = [] {
            std::clog << "[ZDE] Open Recent requested\n";
        },
        .request_open_remote = [] {
            std::clog << "[ZDE] Open Remote requested\n";
        },
        .request_add_folder_to_project = [] {
            std::clog << "[ZDE] Add Folder to Project requested\n";
        },
        .request_save_as = [] {
            std::clog << "[ZDE] Save As requested\n";
        },
        .request_save_all = [] {
            std::clog << "[ZDE] Save All requested\n";
        },
        .request_close_window = [this, win_ptr] { close_window(win_ptr); },
        .request_toggle_terminal = [win_ptr] { win_ptr->toggle_terminal(); },
    });

    if (!view_model->initialize())
    {
        std::cerr << "Fatal error: the Studio command model could not be initialized.\n";
        return nullptr;
    }

    auto* vm_ptr = view_model.get();

    win_ptr->set_command_invoked_callback([vm_ptr](std::string_view command_id) {
        const Commands::CommandExecutionResult result = vm_ptr->execute_command(command_id);
        if (result != Commands::CommandExecutionResult::Executed)
        {
            std::clog << "Command was not executed: " << command_id << '\n';
        }
    });
    win_ptr->set_command_state_query_callback([vm_ptr](std::string_view command_id) {
        const Commands::CommandRegistry& registry = vm_ptr->get_command_registry();
        return Platform::CommandPresentationState{
            .enabled = registry.is_command_enabled(command_id),
            .checked = registry.is_command_checked(command_id),
        };
    });

    if (!win_ptr->initialize())
    {
        std::cerr << "Fatal error: the platform window could not be initialized.\n";
        return nullptr;
    }

    win_ptr->show();

    const std::uint64_t ctx_id = Utility::MultiContextManager::instance().register_context(
        win_ptr, vm_ptr, initial_path);

    auto ctx = std::make_unique<WindowContext>();
    ctx->context_id = ctx_id;
    ctx->window = std::move(platform_window);
    ctx->studio_view_model = std::move(view_model);
    m_windows.push_back(std::move(ctx));

    return win_ptr;
}

bool Application::initialize()
{
    // Bootstrap Language Server, Toolchain, and TextMate Grammars
    Language::Syntax::GrammarRegistry::instance().initialize_default_grammars();
    Language::LanguageServerManager::instance().set_workspace_root(std::filesystem::current_path());
    Language::Toolchain::ToolchainDetector::instance().refresh();

    // Create the initial primary window context
    auto* initial_window = create_new_window();
    return initial_window != nullptr;
}

void Application::show_about(Platform::IPlatformWindow* window) const
{
    if (window != nullptr)
    {
        window->show_about_dialog();
    }
    else if (!m_windows.empty() && m_windows.front()->window)
    {
        m_windows.front()->window->show_about_dialog();
    }
    else
    {
        std::cout << m_specification.name << " v0.1.0\n"
                  << "ZDE-owned application foundation (MVVM).\n";
    }
}

} // namespace Zenvra::Application
