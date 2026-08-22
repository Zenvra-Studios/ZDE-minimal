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
    , m_build_service(std::make_shared<Services::Build::BuildService>())
    , m_execution_service(std::make_shared<Services::Execution::ExecutionService>())
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

        // Frame pacing (2ms sleep yields CPU cleanly while maintaining responsive 500Hz event polling)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    m_execution_service->stop();
    Language::LanguageServerManager::instance().shutdown_all();

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

    auto view_model_holder = std::make_shared<ViewModels::StudioViewModel*>(nullptr);

    auto view_model = std::make_unique<ViewModels::StudioViewModel>(ViewModels::StudioActions{
        .request_close = [this, win_ptr] { close_window(win_ptr); },
        .show_about = [this, win_ptr] { show_about(win_ptr); },
        .request_open_project = [win_ptr] { return win_ptr->open_project_folder(); },
        .request_close_project = [win_ptr] { return win_ptr->close_project(); },
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
        .request_toggle_fullscreen = [win_ptr] { win_ptr->toggle_fullscreen(); },
        .request_reset_layout = [win_ptr] { win_ptr->reset_layout(); },
        .request_minimize_window = [win_ptr] { win_ptr->minimize(); },
        .request_maximize_window = [win_ptr] {
            if (win_ptr->is_maximized()) {
                win_ptr->restore();
            } else {
                win_ptr->maximize();
            }
        },
        .request_toggle_shader = [win_ptr] { win_ptr->toggle_shader_sandbox(); },
        .request_build = [this, win_ptr, view_model_holder] {
            std::string preset = (*view_model_holder) ? std::string((*view_model_holder)->get_active_preset()) : "macos-debug";
            std::string target = (*view_model_holder) ? std::string((*view_model_holder)->get_active_target()) : "ZDE";
            std::filesystem::path ws_root = win_ptr->get_workspace_root();
            if (ws_root.empty()) {
                std::error_code ec;
                ws_root = std::filesystem::current_path(ec);
            }
            Tools::Builder::CMakeBuildOptions opts{
                .workspace_root = ws_root,
                .preset_name = preset,
                .target_name = target
            };
            std::clog << "[ZDE Build] Executing CMake build for target '" << target << "' with preset '" << preset << "'...\n";
            m_build_service->build_async(opts, [](std::string_view log) {
                std::cout << log;
            }, [](bool success) {
                std::clog << "[ZDE Build] " << (success ? "SUCCESS: Target built successfully." : "FAILED: Build errors encountered.") << '\n';
            });
        },
        .request_run = [this, win_ptr, view_model_holder] {
            std::string target = (*view_model_holder) ? std::string((*view_model_holder)->get_active_target()) : "ZDE";
            std::string exec_path = (target == "ZDEUnitTests") ? "bin/Debug/ZDEUnitTests" : "bin/Debug/ZDE.app/Contents/MacOS/ZDE";
            std::filesystem::path ws_root = win_ptr->get_workspace_root();
            if (ws_root.empty()) {
                std::error_code ec;
                ws_root = std::filesystem::current_path(ec);
            }
            Tools::Runner::ProcessExecutionOptions opts{
                .executable_path = exec_path,
                .working_directory = ws_root,
                .run_in_background = true
            };
            std::clog << "[ZDE Run] Launching executable '" << exec_path << "'...\n";
            m_execution_service->run_target_async(opts);
        },
        .request_debug = [] {
            std::clog << "[ZDE Debug] Debug session requested\n";
        },
        .request_stop = [this] {
            m_execution_service->stop();
        },
    });

    *view_model_holder = view_model.get();

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

    if (initial_path && !initial_path->empty())
    {
        static_cast<void>(win_ptr->open_path(*initial_path));
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
    Language::LanguageServerManager::instance().set_workspace_root({});
    Language::Toolchain::ToolchainDetector::instance().refresh();

    // Create the initial primary window context
    auto* initial_window = create_new_window(m_specification.initial_path);
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
        std::cout << m_specification.name << " v" << ZDE_VERSION_STRING << "\n"
                  << "ZDE-owned application foundation (MVVM).\n";
    }
}

} // namespace Zenvra::Application
