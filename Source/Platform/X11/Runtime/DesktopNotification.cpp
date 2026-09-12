#include "Platform/X11/Runtime/DesktopNotification.h"

#include <cstdlib>
#include <dlfcn.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace Zenvra::Platform::X11::Runtime
{

namespace
{

/// Escape a string for single-quoted shell usage.
std::string shell_quote(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('\'');
    for (char c : text) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

bool show_via_libnotify(std::string_view title, std::string_view message)
{
    // dlopen so the X11 backend keeps building/running on systems without
    // libnotify-dev / libnotify at all.
    void* handle = ::dlopen("libnotify.so.4", RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        handle = ::dlopen("libnotify.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (handle == nullptr) return false;

    using NotifyInitFn = bool (*)(const char*);
    using NotifyIsInittedFn = bool (*)();
    using NotifyNotificationNewFn = void* (*)(const char*, const char*, const char*);
    using NotifyNotificationShowFn = bool (*)(void*, void**);

    auto sym = [&](const char* name) { return ::dlsym(handle, name); };
    auto init = reinterpret_cast<NotifyInitFn>(sym("notify_init"));
    auto is_initted = reinterpret_cast<NotifyIsInittedFn>(sym("notify_is_initted"));
    auto notif_new = reinterpret_cast<NotifyNotificationNewFn>(sym("notify_notification_new"));
    auto notif_show = reinterpret_cast<NotifyNotificationShowFn>(sym("notify_notification_show"));

    bool ok = false;
    if (init && notif_new && notif_show) {
        if (is_initted == nullptr || !is_initted()) {
            init("ZDE");
        }
        const std::string t{title};
        const std::string m{message};
        if (void* n = notif_new(t.c_str(), m.c_str(), nullptr)) {
            ok = notif_show(n, nullptr);
            // NOTE: intentionally leak the notification object weigh against
            // linking gobject unref across dlopen boundary versions; the
            // daemon owns the visible lifetime and process-lifetime leak is
            // one small object per notification. See TODO below.
            (void)n;
        }
    }
    ::dlclose(handle);
    return ok;
}

bool show_via_notify_send(std::string_view title, std::string_view message)
{
    // Double-fork so the caller never blocks on the daemon round-trip and
    // never leaves a zombie (grandchild reparented to init).
    const pid_t first = ::fork();
    if (first < 0) return false;
    if (first > 0) {
        int status = 0;
        ::waitpid(first, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    // First child.
    if (::fork() != 0) {
        _exit(0);
    }
    // Grandchild: detached.
    const std::string cmd = std::string("exec notify-send ") +
                            shell_quote(title) + " " + shell_quote(message) +
                            " >/dev/null 2>&1";
    ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

} // namespace

bool DesktopNotification::show(std::string_view title, std::string_view message,
                               std::string_view /*tag*/)
{
    if (title.empty() && message.empty()) return false;
    if (show_via_libnotify(title, message)) return true;
    return show_via_notify_send(title, message);
}

} // namespace Zenvra::Platform::X11::Runtime
