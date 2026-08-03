# Application Architecture

`Zenvra::Application::Application` is the composition root. It creates the
platform window, constructs the Studio ViewModel, injects application actions,
and owns the main loop. `main.cpp` only parses recovery-oriented arguments and
starts this application contract.

The application does not contain platform message handling, menu command
switches, or product panel rendering. Renderer ownership will be added here as
an injected subsystem in the renderer phase.

## MVVM boundary

- Model: `Commands::Command` and `Commands::CommandRegistry`.
- ViewModel: `Application::ViewModels::StudioViewModel`.
- Current View: the native platform window and Win32 fallback menu.
- Future View: ImGui titlebar, dockspace, panels, status bar, and command
  palette.

The View emits stable command IDs. The ViewModel decides whether a command is
enabled and invokes an injected application action. The View never closes the
window or shows product behavior directly.
