#include "WinRTNotification.h"
#include "WinRTContext.h"
#include <windows.h>
#include <iostream>

#if __has_include(<winrt/Windows.Foundation.h>) && \
    __has_include(<winrt/Windows.UI.Notifications.h>) && \
    __has_include(<winrt/Windows.Data.Xml.Dom.h>)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#define ZDE_HAS_WINRT_NOTIFICATION 1
#endif

namespace Zenvra::Platform::Win32::Runtime {

bool WinRTNotification::show_toast(std::wstring_view title, std::wstring_view message, std::wstring_view tag)
{
#if defined(ZDE_HAS_WINRT_NOTIFICATION)
    if (!WinRTContext::initialize()) {
        return false;
    }

    try {
        using namespace winrt::Windows::UI::Notifications;
        using namespace winrt::Windows::Data::Xml::Dom;

        XmlDocument toast_xml = ToastNotificationManager::GetTemplateContent(ToastTemplateType::ToastText02);
        XmlNodeList text_nodes = toast_xml.GetElementsByTagName(L"text");

        if (text_nodes.Length() >= 1) {
            text_nodes.Item(0).AppendChild(toast_xml.CreateTextNode(winrt::hstring(title)));
        }
        if (text_nodes.Length() >= 2) {
            text_nodes.Item(1).AppendChild(toast_xml.CreateTextNode(winrt::hstring(message)));
        }

        ToastNotification toast(toast_xml);
        if (!tag.empty()) {
            toast.Tag(winrt::hstring(tag));
        }

        auto notifier = ToastNotificationManager::CreateToastNotifier(L"Zenvra.ZDE");
        notifier.Show(toast);
        return true;
    } catch (const winrt::hresult_error&) {
        return false;
    } catch (...) {
        return false;
    }
#else
    (void)title;
    (void)message;
    (void)tag;
    return false;
#endif
}

} // namespace Zenvra::Platform::Win32::Runtime

