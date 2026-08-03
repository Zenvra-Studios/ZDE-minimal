#include "Platform/X11/Components/FileDropTarget.h"

#include <X11/Xatom.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Zenvra::Platform::X11::Components
{

namespace
{

constexpr unsigned long maximum_uri_payload_size = 4U * 1024U * 1024U;

int hexadecimal_value(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return character >= 'a' && character <= 'f' ? character - 'a' + 10 : -1;
}

std::string percent_decode(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] == '%' && index + 2 < value.size())
        {
            const int high = hexadecimal_value(value[index + 1]);
            const int low = hexadecimal_value(value[index + 2]);
            if (high >= 0 && low >= 0)
            {
                result.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        result.push_back(value[index]);
    }
    return result;
}

} // namespace

bool FileDropTarget::initialize(Display* display, Window target_window)
{
    shutdown();
    if (display == nullptr || target_window == 0)
    {
        return false;
    }
    m_display = display;
    m_target_window = target_window;
    m_xdnd_aware = XInternAtom(display, "XdndAware", False);
    m_xdnd_enter = XInternAtom(display, "XdndEnter", False);
    m_xdnd_position = XInternAtom(display, "XdndPosition", False);
    m_xdnd_status = XInternAtom(display, "XdndStatus", False);
    m_xdnd_leave = XInternAtom(display, "XdndLeave", False);
    m_xdnd_drop = XInternAtom(display, "XdndDrop", False);
    m_xdnd_finished = XInternAtom(display, "XdndFinished", False);
    m_xdnd_selection = XInternAtom(display, "XdndSelection", False);
    m_xdnd_type_list = XInternAtom(display, "XdndTypeList", False);
    m_xdnd_action_copy = XInternAtom(display, "XdndActionCopy", False);
    m_text_uri_list = XInternAtom(display, "text/uri-list", False);
    m_transfer_property = XInternAtom(display, "ZDE_XDND_DATA", False);

    const unsigned long version = 5;
    XChangeProperty(
        display,
        target_window,
        m_xdnd_aware,
        XA_ATOM,
        32,
        PropModeReplace,
        reinterpret_cast<const unsigned char*>(&version),
        1);
    return true;
}

void FileDropTarget::shutdown() noexcept
{
    m_display = nullptr;
    m_target_window = 0;
    m_source_window = 0;
    m_source_version = 0;
    m_accepts_drop = false;
}

bool FileDropTarget::handle_client_message(const XClientMessageEvent& event)
{
    if (m_display == nullptr || event.window != m_target_window)
    {
        return false;
    }
    if (event.message_type == m_xdnd_enter)
    {
        m_source_window = static_cast<Window>(event.data.l[0]);
        m_source_version = static_cast<unsigned int>(
            (static_cast<unsigned long>(event.data.l[1]) >> 24U) & 0xFFU);
        m_accepts_drop = source_offers_uri_list(event);
        return true;
    }
    if (event.message_type == m_xdnd_position)
    {
        const Window source = static_cast<Window>(event.data.l[0]);
        send_status(source, source == m_source_window && m_accepts_drop);
        return true;
    }
    if (event.message_type == m_xdnd_leave)
    {
        m_source_window = 0;
        m_source_version = 0;
        m_accepts_drop = false;
        return true;
    }
    if (event.message_type == m_xdnd_drop)
    {
        const Window source = static_cast<Window>(event.data.l[0]);
        if (source != m_source_window || !m_accepts_drop)
        {
            send_finished(source, false);
            m_source_window = 0;
            m_source_version = 0;
            m_accepts_drop = false;
            return true;
        }
        const Time event_time = m_source_version >= 1
            ? static_cast<Time>(event.data.l[2])
            : CurrentTime;
        XConvertSelection(
            m_display,
            m_xdnd_selection,
            m_text_uri_list,
            m_transfer_property,
            m_target_window,
            event_time);
        return true;
    }
    return false;
}

std::optional<std::vector<std::filesystem::path>>
FileDropTarget::handle_selection_notify(const XSelectionEvent& event)
{
    if (m_display == nullptr || event.requestor != m_target_window ||
        event.selection != m_xdnd_selection)
    {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> paths;
    bool success = false;
    if (event.property != None)
    {
        Atom actual_type = None;
        int actual_format = 0;
        unsigned long item_count = 0;
        unsigned long remaining_bytes = 0;
        unsigned char* property_data = nullptr;
        const int status = XGetWindowProperty(
            m_display,
            m_target_window,
            event.property,
            0,
            static_cast<long>((maximum_uri_payload_size + 3U) / 4U),
            True,
            AnyPropertyType,
            &actual_type,
            &actual_format,
            &item_count,
            &remaining_bytes,
            &property_data);
        if (status == Success && actual_format == 8 && property_data != nullptr)
        {
            paths = parse_uri_list(property_data, item_count);
            success = !paths.empty();
        }
        if (property_data != nullptr)
        {
            XFree(property_data);
        }
        static_cast<void>(actual_type);
        static_cast<void>(remaining_bytes);
    }
    send_finished(m_source_window, success);
    m_source_window = 0;
    m_source_version = 0;
    m_accepts_drop = false;
    return paths;
}

bool FileDropTarget::source_offers_uri_list(const XClientMessageEvent& event) const
{
    const bool has_type_list = (event.data.l[1] & 1L) != 0;
    if (!has_type_list)
    {
        return static_cast<Atom>(event.data.l[2]) == m_text_uri_list ||
            static_cast<Atom>(event.data.l[3]) == m_text_uri_list ||
            static_cast<Atom>(event.data.l[4]) == m_text_uri_list;
    }

    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long remaining_bytes = 0;
    unsigned char* property_data = nullptr;
    const int status = XGetWindowProperty(
        m_display,
        m_source_window,
        m_xdnd_type_list,
        0,
        256,
        False,
        XA_ATOM,
        &actual_type,
        &actual_format,
        &item_count,
        &remaining_bytes,
        &property_data);
    bool offered = false;
    if (status == Success && actual_type == XA_ATOM && actual_format == 32 &&
        property_data != nullptr)
    {
        const auto* atoms = reinterpret_cast<const Atom*>(property_data);
        offered = std::find(atoms, atoms + item_count, m_text_uri_list) !=
            atoms + item_count;
    }
    if (property_data != nullptr)
    {
        XFree(property_data);
    }
    static_cast<void>(remaining_bytes);
    return offered;
}

void FileDropTarget::send_status(Window source_window, bool accepted) const
{
    if (m_display == nullptr || source_window == 0)
    {
        return;
    }
    XEvent response{};
    response.xclient.type = ClientMessage;
    response.xclient.display = m_display;
    response.xclient.window = source_window;
    response.xclient.message_type = m_xdnd_status;
    response.xclient.format = 32;
    response.xclient.data.l[0] = static_cast<long>(m_target_window);
    response.xclient.data.l[1] = accepted ? 1L : 0L;
    response.xclient.data.l[4] = accepted ? static_cast<long>(m_xdnd_action_copy) : 0L;
    XSendEvent(m_display, source_window, False, NoEventMask, &response);
    XFlush(m_display);
}

void FileDropTarget::send_finished(Window source_window, bool success) const
{
    if (m_display == nullptr || source_window == 0)
    {
        return;
    }
    XEvent response{};
    response.xclient.type = ClientMessage;
    response.xclient.display = m_display;
    response.xclient.window = source_window;
    response.xclient.message_type = m_xdnd_finished;
    response.xclient.format = 32;
    response.xclient.data.l[0] = static_cast<long>(m_target_window);
    response.xclient.data.l[1] = success ? 1L : 0L;
    response.xclient.data.l[2] = success ? static_cast<long>(m_xdnd_action_copy) : 0L;
    XSendEvent(m_display, source_window, False, NoEventMask, &response);
    XFlush(m_display);
}

std::vector<std::filesystem::path> FileDropTarget::parse_uri_list(
    const unsigned char* data,
    std::size_t size)
{
    std::vector<std::filesystem::path> paths;
    if (data == nullptr || size == 0)
    {
        return paths;
    }
    const std::string_view payload{reinterpret_cast<const char*>(data), size};
    std::size_t line_start = 0;
    while (line_start < payload.size())
    {
        const std::size_t line_end = payload.find('\n', line_start);
        std::string_view uri = payload.substr(
            line_start,
            line_end == std::string_view::npos
                ? payload.size() - line_start
                : line_end - line_start);
        if (!uri.empty() && uri.back() == '\r')
        {
            uri.remove_suffix(1);
        }
        if (!uri.empty() && uri.front() != '#')
        {
            constexpr std::string_view file_scheme = "file://";
            if (uri.starts_with(file_scheme))
            {
                uri.remove_prefix(file_scheme.size());
                const std::size_t path_start = uri.find('/');
                if (path_start != std::string_view::npos)
                {
                    const std::string_view authority = uri.substr(0, path_start);
                    if (authority.empty() || authority == "localhost")
                    {
                        paths.emplace_back(percent_decode(uri.substr(path_start)));
                    }
                }
            }
        }
        if (line_end == std::string_view::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }
    return paths;
}

} // namespace Zenvra::Platform::X11::Components
