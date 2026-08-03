#pragma once

#include <X11/Xlib.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace Zenvra::Platform::X11::Components
{

class FileDropTarget
{
public:
    [[nodiscard]] bool initialize(Display* display, Window target_window);
    void shutdown() noexcept;

    [[nodiscard]] bool handle_client_message(const XClientMessageEvent& event);
    [[nodiscard]] std::optional<std::vector<std::filesystem::path>>
        handle_selection_notify(const XSelectionEvent& event);

private:
    [[nodiscard]] bool source_offers_uri_list(const XClientMessageEvent& event) const;
    void send_status(Window source_window, bool accepted) const;
    void send_finished(Window source_window, bool success) const;
    [[nodiscard]] static std::vector<std::filesystem::path> parse_uri_list(
        const unsigned char* data,
        std::size_t size);

    Display* m_display = nullptr;
    Window m_target_window = 0;
    Window m_source_window = 0;
    Atom m_xdnd_aware = None;
    Atom m_xdnd_enter = None;
    Atom m_xdnd_position = None;
    Atom m_xdnd_status = None;
    Atom m_xdnd_leave = None;
    Atom m_xdnd_drop = None;
    Atom m_xdnd_finished = None;
    Atom m_xdnd_selection = None;
    Atom m_xdnd_type_list = None;
    Atom m_xdnd_action_copy = None;
    Atom m_text_uri_list = None;
    Atom m_transfer_property = None;
    unsigned int m_source_version = 0;
    bool m_accepts_drop = false;
};

} // namespace Zenvra::Platform::X11::Components
