#pragma once

#include "UI/Editor/ActivityPanelModel.h"
#include "UI/Editor/StudioEditorModel.h"

#include <X11/Xlib.h>

#include <filesystem>
#include <optional>
#include <string>

namespace Zenvra::Platform::X11::Components
{

class StudioWorkspaceRenderer;

enum class HeaderAction {
    NoneAction,
    NewFile,
    NewFolder,
    Refresh,
    CollapseAll,
    More
};

class ExplorerHeader
{
public:
    ExplorerHeader() = default;

    void render(
        const StudioWorkspaceRenderer& surface,
        Drawable drawable,
        const UI::Editor::StudioEditorLayoutResult& layout,
        const std::string& title,
        bool show_actions = true) const;

    bool handle_pointer_press(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        UI::Editor::ActivityPanelModel& model,
        HeaderAction& action_out,
        bool show_actions = true);

    bool handle_pointer_move(
        const UI::Editor::StudioEditorLayoutResult& layout,
        float point_x,
        float point_y,
        bool show_actions = true) noexcept;

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
        float point_y,
        bool show_actions = true) const noexcept;
    
    ActionIcon m_hovered_icon{ActionIcon::NoneAction};
};

} // namespace Zenvra::Platform::X11::Components
