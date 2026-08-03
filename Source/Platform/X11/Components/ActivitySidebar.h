#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

class ActivitySidebar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    void draw_icon(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        UI::Editor::SidebarIcon icon,
        int center_x,
        int center_y,
        bool active) const;
};

} // namespace Zenvra::Platform::X11::Components
