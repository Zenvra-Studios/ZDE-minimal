#include "Platform/Win32/Components/FileDropTarget.h"

#include <algorithm>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

void FileDropTarget::set_enabled(HWND window_handle, bool enabled) noexcept
{
    if (window_handle != nullptr)
    {
        DragAcceptFiles(window_handle, enabled ? TRUE : FALSE);
    }
}

std::vector<std::filesystem::path> FileDropTarget::collect_paths(HDROP drop)
{
    std::vector<std::filesystem::path> paths;
    if (drop == nullptr)
    {
        return paths;
    }
    const UINT dropped_count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
    paths.reserve(std::min<UINT>(dropped_count, 256U));
    for (UINT index = 0; index < dropped_count && index < 256U; ++index)
    {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        if (DragQueryFileW(drop, index, value.data(), length + 1) != 0)
        {
            value.resize(length);
            paths.emplace_back(std::move(value));
        }
    }
    DragFinish(drop);
    return paths;
}

} // namespace Zenvra::Platform::Win32::Components
