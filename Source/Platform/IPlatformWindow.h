#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Zenvra::Platform
{

struct WindowSpecification
{
    std::string title = "Zenvra Development Studio";
    std::uint32_t width = 1600;
    std::uint32_t height = 900;
    bool custom_chrome_enabled = true;
};

struct WindowCapabilities
{
    bool custom_chrome = false;
    bool native_titlebar_hit_test = false;
    bool native_resize = false;
    bool native_snap = false;
    bool per_monitor_dpi = false;
};

using TitlebarHitTestCallback = std::function<bool(double, double)>;
using CommandInvokedCallback = std::function<void(std::string_view)>;
using WorkspaceChangedCallback = std::function<void(const std::filesystem::path&)>;

struct CommandPresentationState
{
    bool enabled = false;
    bool checked = false;
};

using CommandStateQueryCallback = std::function<CommandPresentationState(std::string_view)>;

class IPlatformWindow
{
public:
    virtual ~IPlatformWindow() = default;

    [[nodiscard]] virtual bool initialize() = 0;
    virtual void show() = 0;
    virtual void poll_events() = 0;
    [[nodiscard]] virtual bool should_close() const = 0;

    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;
    virtual void request_close() = 0;
    virtual void toggle_fullscreen() {}
    virtual void reset_layout() {}

    [[nodiscard]] virtual bool is_maximized() const = 0;
    [[nodiscard]] virtual bool is_minimized() const = 0;
    [[nodiscard]] virtual bool is_focused() const = 0;
    [[nodiscard]] virtual bool is_fullscreen() const { return false; }
    [[nodiscard]] virtual const WindowCapabilities& get_capabilities() const noexcept = 0;
    [[nodiscard]] virtual void* get_native_handle() const noexcept = 0;

    virtual void set_custom_chrome_enabled(bool enabled) = 0;
    virtual void set_titlebar_hit_test_callback(TitlebarHitTestCallback callback) = 0;
    virtual void set_command_invoked_callback(CommandInvokedCallback callback) = 0;
    virtual void set_command_state_query_callback(CommandStateQueryCallback callback) = 0;
    virtual void set_workspace_changed_callback(WorkspaceChangedCallback callback) { (void)callback; }

    /// Prompts the user to select a workspace folder (e.g. "Open Project").
    /// Returns true when the dialog was shown (regardless of the user's
    /// choice); returns false when the platform cannot show a folder dialog.
    [[nodiscard]] virtual bool open_project_folder() { return false; }

    /// Sets the active workspace root folder.
    [[nodiscard]] virtual bool set_workspace_root(const std::filesystem::path& /*root*/) { return false; }

    /// Opens an individual document file.
    [[nodiscard]] virtual bool open_file(const std::filesystem::path& /*path*/) { return false; }

    /// Opens a path (either a directory as workspace root, or a file).
    [[nodiscard]] virtual bool open_path(const std::filesystem::path& /*path*/) { return false; }

    /// Returns the currently opened workspace root folder if any.
    [[nodiscard]] virtual std::filesystem::path get_workspace_root() const { return {}; }

    /// Closes the active project and resets workspace state.
    [[nodiscard]] virtual bool close_project() { return false; }

    /// Toggles the integrated terminal panel visibility.
    virtual void toggle_terminal() {}

    /// Executes a command string in the integrated interactive terminal.
    virtual bool execute_in_terminal(std::string_view /*command*/, const std::filesystem::path& /*working_directory*/ = {}) { return false; }

    /// Shows the bottom panel on the Output channel for build logs.
    virtual void show_output_panel() {}

    /// Refreshes discovered binary configurations in the UI.
    virtual void refresh_configurations() {}

    /// Toggles the shader sandbox panel visibility.
    virtual void toggle_shader_sandbox() {}

    /// Shows the About modal dialog with backdrop blur.
    virtual void show_about_dialog() {}

    /// Returns true if a modal is currently open and active.
    [[nodiscard]] virtual bool is_modal_active() const { return false; }

    /// Gets the currently selected build configuration mode (e.g. "Debug" or "Release").
    [[nodiscard]] virtual std::string get_active_mode() const { return "Debug"; }

    /// Gets the currently selected target architecture (e.g. "x86_64", "x86", "arm64", "arm32").
    [[nodiscard]] virtual std::string get_active_arch() const { return "x86_64"; }

    /// Gets the currently selected binary target name.
    [[nodiscard]] virtual std::string get_active_target_name() const { return {}; }
};

} // namespace Zenvra::Platform
