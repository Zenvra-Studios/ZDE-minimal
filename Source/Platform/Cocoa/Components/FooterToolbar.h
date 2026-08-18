#pragma once

#include "UI/Components/BreadcrumbBar.h"
#include "UI/Editor/StudioEditorModel.h"

#include <CoreGraphics/CoreGraphics.h>

#include <span>
#include <string>

namespace Zenvra::Platform::Cocoa::Components
{

class StudioWorkspaceRenderer;

class FooterToolbar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        CGContextRef context,
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::span<const UI::Editor::BreadcrumbItem> breadcrumbs,
        const UI::Editor::FooterEditorStatus& status) const;

private:
    mutable UI::Components::BreadcrumbBar m_breadcrumb_bar;
};

} // namespace Zenvra::Platform::Cocoa::Components
