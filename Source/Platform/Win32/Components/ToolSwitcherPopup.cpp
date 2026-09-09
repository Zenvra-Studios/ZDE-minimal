#include "Platform/Win32/Components/ToolSwitcherPopup.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Plugins/PluginManager.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cctype>

namespace Zenvra::Platform::Win32::Components
{

using Zenvra::Utility::round_to_int;

void ToolSwitcherPopup::show(float anchor_x, float anchor_y)
{
    m_anchor_x = anchor_x;
    m_anchor_y = anchor_y;
    m_visible = true;
    m_hovered_index.reset();
    m_hovered_browse = false;
    refresh_items();
}

void ToolSwitcherPopup::hide() noexcept
{
    m_visible = false;
    m_hovered_index.reset();
    m_hovered_browse = false;
}

void ToolSwitcherPopup::refresh_items()
{
    m_items.clear();
    auto& pm = Zenvra::Plugins::PluginManager::instance();
    const std::string active_id = pm.get_active_tool_plugin_id();

    for (const auto& p : pm.get_installed_tool_plugins())
    {
        if (!p) continue;
        ToolSwitcherItem item;
        item.id = p->get_id();
        item.name = p->get_name();
        item.category = p->get_manifest().get_category();
        item.is_active = (item.id == active_id);

        std::string name_lower = item.name;
        for (char& c : name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (name_lower.find("docker") != std::string::npos || item.id.find("docker") != std::string::npos)
        {
            item.icon_asset = "material-icon-theme/docker.svg";
        }
        else if (name_lower.find("qemu") != std::string::npos || item.id.find("qemu") != std::string::npos)
        {
            item.icon_asset = "vscode-codicons/icons/chip.svg";
        }
        else if (name_lower.find("gdb") != std::string::npos || name_lower.find("debug") != std::string::npos)
        {
            item.icon_asset = "vscode-codicons/icons/debug-alt.svg";
        }
        else if (name_lower.find("wasm") != std::string::npos)
        {
            item.icon_asset = "vscode-codicons/icons/server.svg";
        }
        else if (name_lower.find("cmake") != std::string::npos || item.id.find("cmake") != std::string::npos)
        {
            item.icon_asset = "Assets/icons/cmake.svg";
        }
        else
        {
            item.icon_asset = "vscode-codicons/icons/tools.svg";
        }

        m_items.push_back(std::move(item));
    }
}

UI::Rect ToolSwitcherPopup::calculate_bounds(float dpi_scale) const noexcept
{
    const float w = 240.0F * dpi_scale;
    const float header_h = 32.0F * dpi_scale;
    const float item_h = 34.0F * dpi_scale;
    const float footer_h = 32.0F * dpi_scale;
    const float content_h = m_items.empty()
        ? 40.0F * dpi_scale
        : (static_cast<float>(m_items.size()) * item_h);
    const float total_h = header_h + content_h + footer_h;

    float y = m_anchor_y - 10.0F * dpi_scale;
    if (y < 30.0F * dpi_scale) y = 30.0F * dpi_scale;

    return UI::Rect{
        m_anchor_x,
        y,
        w,
        total_h
    };
}

bool ToolSwitcherPopup::contains(float point_x, float point_y, float dpi_scale) const noexcept
{
    if (!m_visible) return false;
    return calculate_bounds(dpi_scale).contains(point_x, point_y);
}

ToolSwitcherPopup::PressResult ToolSwitcherPopup::handle_pointer_press(
    float point_x, float point_y, float dpi_scale)
{
    if (!m_visible) return {};

    const UI::Rect bounds = calculate_bounds(dpi_scale);
    if (!bounds.contains(point_x, point_y))
    {
        hide();
        return {};
    }

    const float header_h = 32.0F * dpi_scale;
    const float item_h = 34.0F * dpi_scale;
    float cur_y = bounds.y + header_h;

    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        const UI::Rect item_rect{bounds.x + 4.0F * dpi_scale, cur_y, bounds.width - 8.0F * dpi_scale, item_h};
        if (item_rect.contains(point_x, point_y))
        {
            const std::string sel_id = m_items[i].id;
            hide();
            return PressResult{.handled = true, .switched_tool_id = sel_id};
        }
        cur_y += item_h;
    }

    // Check footer (Browse Marketplace)
    const float footer_h = 32.0F * dpi_scale;
    const UI::Rect footer_rect{bounds.x + 4.0F * dpi_scale, bounds.bottom() - footer_h, bounds.width - 8.0F * dpi_scale, footer_h};
    if (footer_rect.contains(point_x, point_y))
    {
        hide();
        return PressResult{.handled = true, .open_marketplace = true};
    }

    return PressResult{.handled = true};
}

bool ToolSwitcherPopup::handle_pointer_move(
    float point_x, float point_y, float dpi_scale)
{
    if (!m_visible) return false;

    const UI::Rect bounds = calculate_bounds(dpi_scale);
    if (!bounds.contains(point_x, point_y))
    {
        const bool changed = m_hovered_index.has_value() || m_hovered_browse;
        m_hovered_index.reset();
        m_hovered_browse = false;
        return changed;
    }

    const float header_h = 32.0F * dpi_scale;
    const float item_h = 34.0F * dpi_scale;
    float cur_y = bounds.y + header_h;

    std::optional<std::size_t> next_hovered;
    for (std::size_t i = 0; i < m_items.size(); ++i)
    {
        const UI::Rect item_rect{bounds.x + 4.0F * dpi_scale, cur_y, bounds.width - 8.0F * dpi_scale, item_h};
        if (item_rect.contains(point_x, point_y))
        {
            next_hovered = i;
            break;
        }
        cur_y += item_h;
    }

    const float footer_h = 32.0F * dpi_scale;
    const UI::Rect footer_rect{bounds.x + 4.0F * dpi_scale, bounds.bottom() - footer_h, bounds.width - 8.0F * dpi_scale, footer_h};
    const bool next_browse = footer_rect.contains(point_x, point_y);

    const bool changed = (next_hovered != m_hovered_index) || (next_browse != m_hovered_browse);
    m_hovered_index = next_hovered;
    m_hovered_browse = next_browse;
    return changed;
}

void ToolSwitcherPopup::render(
    const StudioWorkspaceRenderer& surface,
    HDC device_context,
    float dpi_scale) const
{
    if (!m_visible) return;

    const UI::Rect bounds = calculate_bounds(dpi_scale);

    // Drop shadow
    surface.fill_rounded_rectangle(
        device_context,
        UI::Rect{bounds.x + 2.0F * dpi_scale, bounds.y + 2.0F * dpi_scale, bounds.width, bounds.height},
        UI::Theme::Color{0, 0, 0, 80},
        6.0F * dpi_scale);

    // Background & Border
    surface.fill_rounded_rectangle(
        device_context,
        bounds,
        UI::Theme::Color{28, 29, 32, 255},
        6.0F * dpi_scale);
    surface.draw_rectangle(
        device_context,
        bounds,
        surface.m_palette.border);

    // Header
    const float header_h = 32.0F * dpi_scale;
    surface.draw_text(
        device_context,
        *surface.m_small_font,
        "SWITCH TOOL VIEW",
        bounds.x + 12.0F * dpi_scale,
        bounds.y + header_h * 0.5F,
        surface.m_palette.text_muted);

    // Header line
    surface.draw_line(
        device_context,
        round_to_int(bounds.x + 8.0F * dpi_scale),
        round_to_int(bounds.y + header_h),
        round_to_int(bounds.right() - 8.0F * dpi_scale),
        round_to_int(bounds.y + header_h),
        surface.m_palette.border);

    // Content
    const float item_h = 34.0F * dpi_scale;
    float cur_y = bounds.y + header_h + 3.0F * dpi_scale;

    if (m_items.empty())
    {
        surface.draw_text(
            device_context,
            *surface.m_small_font,
            "No tool plugins installed",
            bounds.x + 14.0F * dpi_scale,
            cur_y + 14.0F * dpi_scale,
            surface.m_palette.text_muted);
        cur_y += 36.0F * dpi_scale;
    }
    else
    {
        for (std::size_t i = 0; i < m_items.size(); ++i)
        {
            const auto& it = m_items[i];
            const UI::Rect row_rect{bounds.x + 4.0F * dpi_scale, cur_y, bounds.width - 8.0F * dpi_scale, item_h - 2.0F * dpi_scale};

            if (m_hovered_index && *m_hovered_index == i)
            {
                surface.fill_rounded_rectangle(device_context, row_rect, surface.m_palette.hover_background, 4.0F * dpi_scale);
            }

            // Icon
            surface.draw_svg_icon(
                device_context,
                it.icon_asset,
                round_to_int(row_rect.x + 16.0F * dpi_scale),
                round_to_int(row_rect.y + row_rect.height * 0.5F),
                round_to_int(15.0F * dpi_scale),
                surface.m_palette.text_primary,
                surface.m_palette.sidebar_background,
                true);

            // Name
            std::string display_name = it.name;
            if (display_name.size() > 18) display_name = display_name.substr(0, 16) + "...";

            surface.draw_text(
                device_context,
                *surface.m_small_font,
                display_name,
                row_rect.x + 32.0F * dpi_scale,
                row_rect.y + row_rect.height * 0.5F,
                it.is_active ? surface.m_palette.text_primary : surface.m_palette.text_muted);

            // Active checkmark
            if (it.is_active)
            {
                surface.draw_svg_icon(
                    device_context,
                    "check.svg",
                    round_to_int(row_rect.right() - 14.0F * dpi_scale),
                    round_to_int(row_rect.y + row_rect.height * 0.5F),
                    round_to_int(12.0F * dpi_scale),
                    UI::Theme::Color{59, 130, 246, 255}, // accent blue
                    surface.m_palette.sidebar_background,
                    false);
            }

            cur_y += item_h;
        }
    }

    // Separator line before footer
    surface.draw_line(
        device_context,
        round_to_int(bounds.x + 8.0F * dpi_scale),
        round_to_int(cur_y + 2.0F * dpi_scale),
        round_to_int(bounds.right() - 8.0F * dpi_scale),
        round_to_int(cur_y + 2.0F * dpi_scale),
        surface.m_palette.border);

    // Footer: + Browse Marketplace
    const float footer_h = 32.0F * dpi_scale;
    const UI::Rect footer_rect{bounds.x + 4.0F * dpi_scale, bounds.bottom() - footer_h + 2.0F * dpi_scale, bounds.width - 8.0F * dpi_scale, footer_h - 4.0F * dpi_scale};

    if (m_hovered_browse)
    {
        surface.fill_rounded_rectangle(device_context, footer_rect, surface.m_palette.hover_background, 4.0F * dpi_scale);
    }

    surface.draw_svg_icon(
        device_context,
        "vscode-codicons/icons/extensions.svg",
        round_to_int(footer_rect.x + 14.0F * dpi_scale),
        round_to_int(footer_rect.y + footer_rect.height * 0.5F),
        round_to_int(12.0F * dpi_scale),
        UI::Theme::Color{56, 189, 248, 255},
        surface.m_palette.sidebar_background,
        true);

    surface.draw_text(
        device_context,
        *surface.m_small_font,
        "Browse Marketplace...",
        footer_rect.x + 28.0F * dpi_scale,
        footer_rect.y + footer_rect.height * 0.5F,
        UI::Theme::Color{56, 189, 248, 255}); // sky-400
}

} // namespace Zenvra::Platform::Win32::Components
