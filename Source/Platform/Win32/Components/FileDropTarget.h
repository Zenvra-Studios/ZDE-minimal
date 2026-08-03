#pragma once

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <vector>

namespace Zenvra::Platform::Win32::Components
{

class FileDropTarget
{
public:
    static void set_enabled(HWND window_handle, bool enabled) noexcept;
    [[nodiscard]] static std::vector<std::filesystem::path> collect_paths(HDROP drop);
};

} // namespace Zenvra::Platform::Win32::Components
