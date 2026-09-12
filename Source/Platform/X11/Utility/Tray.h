#pragma once

#include <X11/Xlib.h>

#include <functional>
#include <string>

namespace Zenvra::Platform::X11
{

/// Tray command ids mirroring Win32 TrayCommandId (kept numerically identical
/// so cross-platform docs stay in sync).
enum TrayCommandId : unsigned int
{
    TrayCmdShow = 60001,
    TrayCmdHide = 60002,
    TrayCmdNewFile = 60003,
    TrayCmdOpenFolder = 60004,
    TrayCmdExit = 60005,
};

/// Native X11 system-tray icon (XEMBED systray spec, no GTK dependency).
///
/// Docks a small 24x24 window into the panel's systray owner
/// (_NET_SYSTEM_TRAY_S0). If no systray owner exists (e.g. minimal WM without
/// a panel), create() returns false and the object stays inert — callers must
/// fall back to plain minimize.
class SystemTray
{
public:
    enum class ClickAction
    {
        NoAction,
        Activate,    ///< left click -> restore/show
        ContextMenu, ///< right (or middle) click -> open menu
    };

    SystemTray();
    ~SystemTray();

    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    /// Dock the icon. Returns false when no systray owner is available.
    bool create(Display* display, int screen, const std::string& tooltip = "ZDE - Zenvra Development Environment");
    void destroy();

    void set_tooltip(const std::string& tooltip);
    void set_visible(bool visible);
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }
    [[nodiscard]] bool is_docked() const noexcept { return m_docked; }
    [[nodiscard]] Window window() const noexcept { return m_tray_window; }

    /// Route an XEvent; returns click action for ButtonPress on the icon.
    /// Also repaints on Expose. Returns ClickAction::NoAction for other events.
    [[nodiscard]] ClickAction handle_event(const XEvent& event);

    /// Desktop notification via DesktopNotification helper (libnotify /
    /// notify-send fallback). Never blocks, never throws.
    void show_notification(const std::string& title, const std::string& message);

private:
    void paint();
    Atom tray_selection_atom() const;
    Window find_tray_owner() const;

    Display* m_display = nullptr;
    int m_screen = 0;
    Window m_tray_window = 0;
    GC m_gc = nullptr;
    std::string m_tooltip;
    bool m_visible = false;
    bool m_docked = false;
};

} // namespace Zenvra::Platform::X11
