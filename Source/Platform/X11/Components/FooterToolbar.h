#pragma once

#include "UI/Components/BreadcrumbBar.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <span>
#include <string>
#include <iostream>

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

private:
    mutable UI::Components::BreadcrumbBar m_breadcrumb_bar;
};

} // namespace Zenvra::Platform::X11::Components
