#include "Platform/X11/Utility/Tray.h"
#include "Platform/X11/Runtime/DesktopNotification.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstring>

namespace Zenvra::Platform::X11
{

namespace
{
constexpr int kTraySize = 24;
constexpr long kSystemTrayRequestDock = 0;
constexpr long kXembedMapped = 1;
} // namespace

SystemTray::SystemTray() = default;

SystemTray::~SystemTray()
{
    destroy();
}

Atom SystemTray::tray_selection_atom() const
{
    char name[64]{};
    std::snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", m_screen);
    return XInternAtom(m_display, name, False);
}

Window SystemTray::find_tray_owner() const
{
    if (m_display == nullptr) return 0;
    return XGetSelectionOwner(m_display, tray_selection_atom());
}

bool SystemTray::create(Display* display, int screen, const std::string& tooltip)
{
    if (m_docked) return true;
    if (display == nullptr) return false;
    m_display = display;
    m_screen = screen;
    m_tooltip = tooltip;

    const Window owner = find_tray_owner();
    if (owner == 0) {
        // No systray host (panel) running — stay inert.
        return false;
    }

    XSetWindowAttributes attrs{};
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask;
    attrs.background_pixel = 0;
    m_tray_window = XCreateWindow(
        m_display, RootWindow(m_display, m_screen), 0, 0,
        kTraySize, kTraySize, 0, CopyFromParent, InputOutput, CopyFromParent,
        CWEventMask | CWBackPixel, &attrs);
    if (m_tray_window == 0) return false;

    m_gc = XCreateGC(m_display, m_tray_window, 0, nullptr);

    // Tooltip: both legacy and EWMH names.
    set_tooltip(m_tooltip);

    // XEMBED info: version 0, mapped.
    const long xembed_info[2] = {0, kXembedMapped};
    const Atom xembed_atom = XInternAtom(m_display, "_XEMBED_INFO", False);
    XChangeProperty(m_display, m_tray_window, xembed_atom, xembed_atom, 32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(xembed_info), 2);

    // Ask the tray owner to dock us.
    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = owner;
    ev.xclient.message_type = XInternAtom(m_display, "_NET_SYSTEM_TRAY_OPCODE", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = kSystemTrayRequestDock;
    ev.xclient.data.l[2] = static_cast<long>(m_tray_window);
    ev.xclient.data.l[3] = 0;
    ev.xclient.data.l[4] = 0;
    XSendEvent(m_display, owner, False, NoEventMask, &ev);
    XSync(m_display, False);

    XMapWindow(m_display, m_tray_window);
    XFlush(m_display);

    m_docked = true;
    m_visible = true;
    paint();
    return true;
}

void SystemTray::destroy()
{
    if (m_display != nullptr) {
        if (m_gc != nullptr) {
            XFreeGC(m_display, m_gc);
            m_gc = nullptr;
        }
        if (m_tray_window != 0) {
            XDestroyWindow(m_display, m_tray_window);
            m_tray_window = 0;
        }
        XFlush(m_display);
    }
    m_docked = false;
    m_visible = false;
}

void SystemTray::set_tooltip(const std::string& tooltip)
{
    m_tooltip = tooltip;
    if (m_display == nullptr || m_tray_window == 0) return;
    const Atom utf8 = XInternAtom(m_display, "UTF8_STRING", False);
    XChangeProperty(m_display, m_tray_window, XA_WM_NAME, utf8, 8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(m_tooltip.data()),
                    static_cast<int>(m_tooltip.size()));
    const Atom net_name = XInternAtom(m_display, "_NET_WM_NAME", False);
    XChangeProperty(m_display, m_tray_window, net_name, utf8, 8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(m_tooltip.data()),
                    static_cast<int>(m_tooltip.size()));
    XFlush(m_display);
}

void SystemTray::set_visible(bool visible)
{
    if (m_visible == visible) return;
    m_visible = visible;
    if (m_display == nullptr || m_tray_window == 0) return;
    if (visible) {
        XMapWindow(m_display, m_tray_window);
    } else {
        XUnmapWindow(m_display, m_tray_window);
    }
    XFlush(m_display);
}

void SystemTray::show_notification(const std::string& title, const std::string& message)
{
    Runtime::DesktopNotification::show(title, message, "ZDE_TRAY");
}

SystemTray::ClickAction SystemTray::handle_event(const XEvent& event)
{
    if (m_tray_window == 0 || event.xany.window != m_tray_window) {
        return ClickAction::NoAction;
    }
    if (event.type == Expose) {
        paint();
        return ClickAction::NoAction;
    }
    if (event.type == ButtonPress) {
        const unsigned int button = event.xbutton.button;
        if (button == Button1) return ClickAction::Activate;
        if (button == Button3 || button == Button2) return ClickAction::ContextMenu;
    }
    return ClickAction::NoAction;
}

void SystemTray::paint()
{
    if (m_display == nullptr || m_tray_window == 0 || m_gc == nullptr) return;

    // Panel-like dark rounded background.
    XSetForeground(m_display, m_gc, 0x1C1D20UL);
    XFillRectangle(m_display, m_tray_window, m_gc, 0, 0, kTraySize, kTraySize);

    // Accent border.
    XSetForeground(m_display, m_gc, 0x0E639CUL);
    XDrawRectangle(m_display, m_tray_window, m_gc, 0, 0, kTraySize - 1, kTraySize - 1);

    // White "Z" glyph (stylized like the fallback window icon).
    XSetForeground(m_display, m_gc, 0xFFFFFFUL);
    XSetLineAttributes(m_display, m_gc, 2, LineSolid, CapRound, JoinRound);
    XDrawLine(m_display, m_tray_window, m_gc, 6, 6, 18, 6);
    XDrawLine(m_display, m_tray_window, m_gc, 18, 6, 6, 18);
    XDrawLine(m_display, m_tray_window, m_gc, 6, 18, 18, 18);
    XFlush(m_display);
}

} // namespace Zenvra::Platform::X11
