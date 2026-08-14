#pragma once

#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <windows.h>
#include <optional>
#include <filesystem>
#include <string>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

class ExplorerHeader
{
public:
    ExplorerHeader() = default;

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        const UI::Editor::StudioEditorLayoutResult& layout,
        const std::string& title) const;

    bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        UI::Editor::ActivityPanelModel& model,
        std::optional<std::filesystem::path>& file_to_open);

    bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) noexcept;

private:
    enum class ActionIcon {
        NoneAction,
        NewFile,
        NewFolder,
        Refresh,
        CollapseAll,
        More
    };
    
    [[nodiscard]] ActionIcon get_icon_at_point(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y) const noexcept;
    
    ActionIcon m_hovered_icon{ActionIcon::NoneAction};
};

} // namespace Zenvra::Platform::Win32::Components
