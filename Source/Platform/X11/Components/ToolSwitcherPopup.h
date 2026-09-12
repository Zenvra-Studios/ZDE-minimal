#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <X11/Xlib.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Zenvra::Platform::X11::Components
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

/// Tool switcher popup ported from Win32 (same logic, X11 rendering).
/// Shows installed tool plugins (Docker, QEMU, GDB, ...) with active checkmark
/// and a "Browse Marketplace..." footer. Logic is platform-agnostic; only
/// render() uses X11 Drawable via StudioWorkspaceRenderer helpers.
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
        Drawable drawable,
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

} // namespace Zenvra::Platform::X11::Components
