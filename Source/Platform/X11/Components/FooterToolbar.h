#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <span>
#include <string>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

class FooterToolbar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::span<const UI::Editor::BreadcrumbItem> breadcrumbs,
        const UI::Editor::FooterEditorStatus& status) const;
};

} // namespace Zenvra::Platform::X11::Components
