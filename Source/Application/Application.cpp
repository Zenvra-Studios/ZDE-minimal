#include "Application/Application.h"
#include "Language/LanguageServerManager.h"
#include "Language/Syntax/GrammarRegistry.h"
#include "Language/Toolchain/ToolchainDetector.h"
#include "Platform/HostSystem.h"
#include "Platform/PlatformWindowFactory.h"
#include "Plugins/PluginManager.h"
#include "Services/Output/OutputLogManager.h"
#include "Tools/Classification/ProjectToolClassifier.h"
#include "UI/Theme/ThemeManager.h"
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
    , m_debugger_engine(std::make_shared<Tools::Debugger::DebuggerEngine>())
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

    std::filesystem::path ws_init;
    if (initial_path && !initial_path->empty()) {
        std::error_code ec;
        if (std::filesystem::is_directory(*initial_path, ec)) {
            ws_init = *initial_path;
        }
    }

    auto view_model = std::make_unique<ViewModels::StudioViewModel>(ViewModels::StudioActions{
        .request_close = [this, win_ptr] { close_window(win_ptr); },
        .show_about = [this, win_ptr] { show_about(win_ptr); },
        .request_open_project = [win_ptr, view_model_holder] {
            const bool ok = win_ptr->open_project_folder();
            if (ok && *view_model_holder) {
                (*view_model_holder)->configure_for_workspace(win_ptr->get_workspace_root());
            }
            return ok;
        },
        .request_close_project = [win_ptr, view_model_holder] {
            const bool ok = win_ptr->close_project();
            if (ok && *view_model_holder) {
                (*view_model_holder)->configure_for_workspace({});
            }
            return ok;
        },
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
            std::filesystem::path ws_root = win_ptr->get_workspace_root();
            if (ws_root.empty()) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Build,
                    "[Build] No project or workspace folder is currently opened.");
                return;
            }
            const std::string default_target = ws_root.filename().empty() ? "Project" : ws_root.filename().string();
            std::string preset = (*view_model_holder) ? std::string((*view_model_holder)->get_active_preset()) : Platform::HostSystem::get_system_info().default_preset_debug;
            std::string target = (*view_model_holder) ? std::string((*view_model_holder)->get_active_target()) : default_target;
            std::string mode = win_ptr ? win_ptr->get_active_mode() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_mode()) : "Debug");
            std::string arch = win_ptr ? win_ptr->get_active_arch() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_arch()) : "x86_64");
            if (*view_model_holder) {
                (*view_model_holder)->set_active_mode(mode);
                (*view_model_holder)->set_active_arch(arch);
            }
            UI::Toolbar::ToolClassification classification = (*view_model_holder) ? (*view_model_holder)->get_active_classification() : UI::Toolbar::ToolClassification::CMake;

            // Show output channel in bottom panel
            win_ptr->show_output_panel();

            if (classification == UI::Toolbar::ToolClassification::Python)
            {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Build,
                    "[Build: Python] Target is a Python script/project; no build step required.");
                return;
            }

            if (classification == UI::Toolbar::ToolClassification::TomlCargo)
            {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Build,
                    "[Build: Cargo] Building Cargo target '" + target + "' (" + mode + ", " + arch + ")...");
                std::vector<std::string> args = {"build"};
                if (mode == "Release" || preset.find("release") != std::string::npos || preset.find("Release") != std::string::npos)
                {
                    args.push_back("--release");
                }
                if (!target.empty() && target != "Cargo Run" && target != ws_root.filename().string())
                {
                    args.push_back("--bin");
                    args.push_back(target);
                }
#if defined(_WIN32)
                if (arch == "x86")
                {
                    args.push_back("--target");
                    args.push_back("i686-pc-windows-msvc");
                }
                else if (arch == "arm64")
                {
                    args.push_back("--target");
                    args.push_back("aarch64-pc-windows-msvc");
                }
#endif
                Tools::Runner::ProcessExecutionOptions opts{
                    .executable_path = "cargo",
                    .arguments = args,
                    .working_directory = ws_root,
                    .run_in_background = true
                };
                m_execution_service->run_target_async(opts, [](std::string_view log) {
                    Services::Output::OutputLogManager::instance().append_text(Services::Output::OutputCategory::Build, log);
                });
                return;
            }

            if (classification == UI::Toolbar::ToolClassification::Pascal)
            {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Build,
                    "[Build: Pascal] Building Pascal target '" + target + "'...");
                std::string exec_or_script = (*view_model_holder) ? std::string((*view_model_holder)->get_active_executable_path()) : "";
                Tools::Runner::ProcessExecutionOptions opts{
                    .executable_path = "fpc",
                    .arguments = {"-B", exec_or_script.empty() ? target : exec_or_script},
                    .working_directory = ws_root,
                    .run_in_background = true
                };
                m_execution_service->run_target_async(opts, [](std::string_view log) {
                    Services::Output::OutputLogManager::instance().append_text(Services::Output::OutputCategory::Build, log);
                });
                return;
            }

            // Default CMake build
            Tools::Builder::CMakeBuildOptions opts{
                .workspace_root = ws_root,
                .preset_name = preset,
                .target_name = target,
                .build_directory = "",
                .configuration = mode,
                .architecture = arch
            };
            std::clog << "[ZDE Build] Executing CMake build for target '" << target << "' [" << mode << " | " << arch << "]...\n";
            Services::Output::OutputLogManager::instance().append_line(
                Services::Output::OutputCategory::Build,
                "[Build: CMake] Executing build for target '" + target + "' [" + mode + " | " + arch + "]...");
            m_build_service->build_async(opts, [](std::string_view log) {
                Services::Output::OutputLogManager::instance().append_text(Services::Output::OutputCategory::Build, log);
                std::cout << log;
            }, [win_ptr](bool success) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Build,
                    success ? "[Build] SUCCESS: Target built successfully." : "[Build] FAILED: Build errors encountered.");
                if (success) {
                    win_ptr->refresh_configurations();
                }
            });
        },
        .request_run = [this, win_ptr, view_model_holder] {
            std::filesystem::path ws_root = win_ptr->get_workspace_root();
            if (ws_root.empty()) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Run] No project or workspace folder is currently opened.");
                return;
            }
            const std::string default_target = ws_root.filename().empty() ? "Project" : ws_root.filename().string();
            std::string target = win_ptr ? win_ptr->get_active_target_name() : "";
            if (target.empty() && *view_model_holder) {
                target = std::string((*view_model_holder)->get_active_target());
            }
            if (target.empty() || target == "No Configuration") {
                target = default_target;
            }
            std::string mode = win_ptr ? win_ptr->get_active_mode() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_mode()) : "Debug");
            std::string arch = win_ptr ? win_ptr->get_active_arch() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_arch()) : "x86_64");
            if (*view_model_holder) {
                (*view_model_holder)->set_active_mode(mode);
                (*view_model_holder)->set_active_arch(arch);
            }
            std::string preset = (*view_model_holder) ? std::string((*view_model_holder)->get_active_preset()) : Platform::HostSystem::get_system_info().default_preset_debug;
            UI::Toolbar::ToolClassification classification = (*view_model_holder) ? (*view_model_holder)->get_active_classification() : UI::Toolbar::ToolClassification::CMake;
            std::string exec_or_script = (*view_model_holder) ? std::string((*view_model_holder)->get_active_executable_path()) : "";
            const std::string config = (mode == "Release" || preset.find("release") != std::string::npos || preset.find("Release") != std::string::npos) ? "Release" : "Debug";

            UI::Toolbar::BinaryTargetProfile profile;
            profile.name = target;
            profile.classification = classification;
            profile.executable_path = exec_or_script;

            const auto cmd = Tools::Classification::ProjectToolClassifier::create_run_command(profile, ws_root, preset, config, arch);
            std::clog << "[ZDE Runner] [" << UI::Toolbar::to_string(classification) << "] Running '" << cmd.program << "' for target '" << target << "' [" << config << " | " << arch << "]...\n";

            std::error_code ec;
            std::filesystem::path prog_path(cmd.program);
            const bool binary_exists = std::filesystem::exists(prog_path, ec) ||
                                       std::filesystem::exists(ws_root / prog_path, ec) ||
                                       std::filesystem::exists(cmd.working_directory / prog_path, ec);

            if (!binary_exists &&
                (classification == UI::Toolbar::ToolClassification::CMake ||
                 classification == UI::Toolbar::ToolClassification::CustomExecutable ||
                 classification == UI::Toolbar::ToolClassification::Pascal)) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Run] Binary not found for target '" + target + "' (" + config + " | " + arch + "): " + cmd.program);
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Run] Please build the project first using the Hammer icon (Build) or Ctrl+Shift+B.");
                win_ptr->show_output_panel();
                return;
            }

            std::string term_cmd;
#if defined(_WIN32)
            if (cmd.program.find(' ') != std::string::npos ||
                cmd.program.find('\\') != std::string::npos ||
                cmd.program.find('/') != std::string::npos) {
                term_cmd = "& \"" + cmd.program + "\"";
            } else {
                term_cmd = cmd.program;
            }
#else
            if (cmd.program.find(' ') != std::string::npos) {
                term_cmd = "\"" + cmd.program + "\"";
            } else {
                term_cmd = cmd.program;
            }
#endif
            for (const auto& arg : cmd.arguments) {
                if (arg.find(' ') != std::string::npos) {
                    term_cmd += " \"" + arg + "\"";
                } else {
                    term_cmd += " " + arg;
                }
            }

            Services::Output::OutputLogManager::instance().append_line(
                Services::Output::OutputCategory::Runner,
                "[Run: " + std::string(UI::Toolbar::to_string(classification)) + "] Executing " + term_cmd + " in terminal for " + target);

            const bool launched_in_terminal = win_ptr->execute_in_terminal(term_cmd, cmd.working_directory);
            if (!launched_in_terminal) {
                Tools::Runner::ProcessExecutionOptions opts{
                    .executable_path = cmd.program,
                    .arguments = cmd.arguments,
                    .working_directory = cmd.working_directory,
                    .run_in_background = true
                };
                m_execution_service->run_target_async(opts, [](std::string_view log) {
                    Services::Output::OutputLogManager::instance().append_text(Services::Output::OutputCategory::Runner, log);
                });
            }
        },
        .request_debug = [this, win_ptr, view_model_holder] {
            std::filesystem::path ws_root = win_ptr->get_workspace_root();
            if (ws_root.empty()) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Debug] No project or workspace folder is currently opened.");
                return;
            }
            const std::string default_target = ws_root.filename().empty() ? "Project" : ws_root.filename().string();
            std::string target = win_ptr ? win_ptr->get_active_target_name() : "";
            if (target.empty() && *view_model_holder) {
                target = std::string((*view_model_holder)->get_active_target());
            }
            if (target.empty() || target == "No Configuration") {
                target = default_target;
            }
            std::string mode = win_ptr ? win_ptr->get_active_mode() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_mode()) : "Debug");
            std::string arch = win_ptr ? win_ptr->get_active_arch() : ((*view_model_holder) ? std::string((*view_model_holder)->get_active_arch()) : "x86_64");
            if (*view_model_holder) {
                (*view_model_holder)->set_active_mode(mode);
                (*view_model_holder)->set_active_arch(arch);
            }
            std::string preset = (*view_model_holder) ? std::string((*view_model_holder)->get_active_preset()) : Platform::HostSystem::get_system_info().default_preset_debug;
            UI::Toolbar::ToolClassification classification = (*view_model_holder) ? (*view_model_holder)->get_active_classification() : UI::Toolbar::ToolClassification::CMake;
            std::string exec_or_script = (*view_model_holder) ? std::string((*view_model_holder)->get_active_executable_path()) : "";
            const std::string config = (mode == "Release" || preset.find("release") != std::string::npos || preset.find("Release") != std::string::npos) ? "Release" : "Debug";

            UI::Toolbar::BinaryTargetProfile profile;
            profile.name = target;
            profile.classification = classification;
            profile.executable_path = exec_or_script;

            const auto cmd = Tools::Classification::ProjectToolClassifier::create_debug_command(profile, ws_root, preset, config, arch);
            std::clog << "[ZDE Debug] [" << UI::Toolbar::to_string(classification) << "] Starting debug session with '" << cmd.program << "' for target '" << target << "' [" << config << " | " << arch << "]...\n";

            std::error_code ec;
            std::filesystem::path prog_path(cmd.program);
            const bool binary_exists = std::filesystem::exists(prog_path, ec) ||
                                       std::filesystem::exists(ws_root / prog_path, ec) ||
                                       std::filesystem::exists(cmd.working_directory / prog_path, ec);

            if (!binary_exists &&
                (classification == UI::Toolbar::ToolClassification::CMake ||
                 classification == UI::Toolbar::ToolClassification::CustomExecutable ||
                 classification == UI::Toolbar::ToolClassification::Pascal)) {
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Debug] Binary not found for target '" + target + "' (" + config + " | " + arch + "): " + cmd.program);
                Services::Output::OutputLogManager::instance().append_line(
                    Services::Output::OutputCategory::Runner,
                    "[Debug] Please build the project first using the Hammer icon (Build) or Ctrl+Shift+B.");
                win_ptr->show_output_panel();
                return;
            }

            std::string term_cmd;
#if defined(_WIN32)
            if (cmd.program.find(' ') != std::string::npos ||
                cmd.program.find('\\') != std::string::npos ||
                cmd.program.find('/') != std::string::npos) {
                term_cmd = "& \"" + cmd.program + "\"";
            } else {
                term_cmd = cmd.program;
            }
#else
            if (cmd.program.find(' ') != std::string::npos) {
                term_cmd = "\"" + cmd.program + "\"";
            } else {
                term_cmd = cmd.program;
            }
#endif
            for (const auto& arg : cmd.arguments) {
                if (arg.find(' ') != std::string::npos) {
                    term_cmd += " \"" + arg + "\"";
                } else {
                    term_cmd += " " + arg;
                }
            }

            Services::Output::OutputLogManager::instance().append_line(
                Services::Output::OutputCategory::Runner,
                "[Debug: " + std::string(UI::Toolbar::to_string(classification)) + "] Starting " + term_cmd + " in terminal for " + target);

            const bool launched_in_terminal = win_ptr->execute_in_terminal(term_cmd, cmd.working_directory);
            if (!launched_in_terminal) {
                Tools::Debugger::DebugSessionOptions dbg_opts{
                    .target_binary = cmd.program,
                    .target_arguments = cmd.arguments,
                    .working_directory = cmd.working_directory,
                    .backend = Tools::Debugger::DebuggerBackend::LLDB
                };
                m_debugger_engine->start_session(dbg_opts, [](std::string_view msg) {
                    Services::Output::OutputLogManager::instance().append_line(Services::Output::OutputCategory::Runner, msg);
                });
            }
        },
        .request_stop = [this] {
            m_execution_service->stop();
            Services::Output::OutputLogManager::instance().append_line(
                Services::Output::OutputCategory::Runner,
                "[Runner] Stop requested. Active process stopped.");
        },
    }, ws_init);

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
    win_ptr->set_workspace_changed_callback([vm_ptr](const std::filesystem::path& ws) {
        if (vm_ptr) {
            vm_ptr->configure_for_workspace(ws);
        }
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

    // Initialize Plugin & Toolchain subsystem (Local-first / Git-first)
    Plugins::PluginManager::instance().initialize();

    // Initialize Theme subsystem and sync themes with Settings
    UI::Theme::ThemeManager::instance().initialize();

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
