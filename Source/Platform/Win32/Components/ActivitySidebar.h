#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class ActivitySidebar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    void draw_icon(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        UI::Editor::SidebarIcon icon,
        int center_x,
        int center_y,
        bool active,
        bool hovered) const;
};

} // namespace Zenvra::Platform::Win32::Components
