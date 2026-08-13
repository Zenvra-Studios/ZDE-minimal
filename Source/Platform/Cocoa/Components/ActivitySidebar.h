#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <CoreGraphics/CoreGraphics.h>

namespace Zenvra::Platform::Cocoa::Components
{

class StudioWorkspaceRenderer;

class ActivitySidebar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout) const;

private:
    void draw_icon(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        UI::Editor::SidebarIcon icon,
        int center_x, int center_y,
        bool active) const;
};

} // namespace Zenvra::Platform::Cocoa::Components
