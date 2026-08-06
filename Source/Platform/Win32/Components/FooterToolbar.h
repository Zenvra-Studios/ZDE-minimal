#pragma once

#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>

#include <span>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class FooterToolbar
{
public:
    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout,
        std::span<const UI::Editor::BreadcrumbItem> breadcrumbs,
        const UI::Editor::FooterEditorStatus& status) const;
};

} // namespace Zenvra::Platform::Win32::Components
