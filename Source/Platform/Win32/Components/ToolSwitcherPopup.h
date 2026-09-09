#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace Zenvra::Platform::Win32::Components
{

class StudioWorkspaceRenderer;

struct ToolSwitcherItem
{
    std::string id;
    std::string name;
    std::string category;
    std::string icon_asset;
    bool is_active{false};
};

class ToolSwitcherPopup
{
public:
    struct PressResult
    {
        bool handled{false};
        std::string switched_tool_id;
        bool open_marketplace{false};
    };

    ToolSwitcherPopup() = default;

    void show(float anchor_x, float anchor_y);
    void hide() noexcept;
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    [[nodiscard]] UI::Rect calculate_bounds(float dpi_scale) const noexcept;
    [[nodiscard]] bool contains(float point_x, float point_y, float dpi_scale) const noexcept;

    PressResult handle_pointer_press(float point_x, float point_y, float dpi_scale);
    bool handle_pointer_move(float point_x, float point_y, float dpi_scale);

    void render(
        const StudioWorkspaceRenderer& surface,
        HDC device_context,
        float dpi_scale) const;

private:
    void refresh_items();

    bool m_visible{false};
    float m_anchor_x{0.0F};
    float m_anchor_y{0.0F};

    std::vector<ToolSwitcherItem> m_items;
    std::optional<std::size_t> m_hovered_index;
    bool m_hovered_browse{false};
};

} // namespace Zenvra::Platform::Win32::Components
