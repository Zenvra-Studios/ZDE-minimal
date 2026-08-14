#include "UI/Components/AboutModal.h"

#include <algorithm>
#include <sstream>

namespace Zenvra::UI::Components {

AboutModal::AboutModal()
    : m_theme(ModalTheme::from_theme(Theme::StudioTheme::zenvra_dark()))
{
#if defined(_WIN32)
    m_platform = "Windows - 64Bit";
#elif defined(__APPLE__)
    m_platform = "macOS - Apple Silicon / x64";
#else
    m_platform = "Linux - 64Bit";
#endif

    m_specs = {
        {"Version", "0.1.0-preview"},
        {"Studio", "Zenvra Studios"},
        {"Engine", "ZDE Native MVVM Engine"},
        {"Graphics", "OpenGL Core"},
        {"Commit", "zde-minimal-main (Release)"},
        {"Date", "2026-08-14"},
#if defined(_WIN32)
        {"OS", "Windows_NT x64 10.0.19045"},
#elif defined(__APPLE__)
        {"OS", "Darwin x64 / ARM64"},
#else
        {"OS", "Linux x86_64"},
#endif
        {"Language Server", "Clang/LLVM C++20 / Clangd"}
    };
}

void AboutModal::close() noexcept {
    m_visible = false;
    m_close_hovered = false;
    m_close_pressed = false;
    m_copy_hovered = false;
    m_copy_pressed = false;
    m_ok_hovered = false;
    m_ok_pressed = false;
    m_backdrop_pressed = false;
}

std::string AboutModal::get_clipboard_text() const {
    std::ostringstream ss;
    ss << m_app_name << "\n";
    ss << m_edition << "\n";
    ss << "Studio: " << m_studio_name << "\n";
    ss << "Created by: " << m_created_by << "\n";
    ss << "Platform: " << m_platform << "\n";
    ss << "----------------------------------------\n";
    for (const auto& spec : m_specs) {
        ss << spec.key << ": " << spec.value << "\n";
    }
    return ss.str();
}

AboutModalLayoutResult AboutModal::calculate_layout(const Rect& viewport_bounds, float dpi_scale) const noexcept {
    AboutModalLayoutResult layout{};
    layout.dpi_scale = std::max(dpi_scale, 0.5F);
    layout.base_layout.dpi_scale = layout.dpi_scale;
    layout.base_layout.backdrop_bounds = viewport_bounds;

    const float modal_w = std::min(480.0F * layout.dpi_scale, std::max(100.0F, viewport_bounds.width - 32.0F * layout.dpi_scale));
    const float modal_h = std::min(360.0F * layout.dpi_scale, std::max(100.0F, viewport_bounds.height - 32.0F * layout.dpi_scale));
    const float modal_x = viewport_bounds.x + (viewport_bounds.width - modal_w) * 0.5F;
    const float modal_y = viewport_bounds.y + (viewport_bounds.height - modal_h) * 0.5F;

    layout.base_layout.dialog_bounds = Rect{modal_x, modal_y, modal_w, modal_h};

    const float close_size = 24.0F * layout.dpi_scale;
    layout.close_button_bounds = Rect{
        modal_x + modal_w - close_size - 12.0F * layout.dpi_scale,
        modal_y + 12.0F * layout.dpi_scale,
        close_size,
        close_size
    };
    layout.base_layout.close_button_bounds = layout.close_button_bounds;

    // Header bounds: Logo + Title + Subtitle
    const float logo_sz = 40.0F * layout.dpi_scale;
    layout.logo_bounds = Rect{
        modal_x + 24.0F * layout.dpi_scale,
        modal_y + 20.0F * layout.dpi_scale,
        logo_sz,
        logo_sz
    };

    const float title_x = layout.logo_bounds.right() + 14.0F * layout.dpi_scale;
    const float title_w = modal_x + modal_w - title_x - close_size - 16.0F * layout.dpi_scale;

    layout.headline_bounds = Rect{
        title_x,
        modal_y + 20.0F * layout.dpi_scale,
        title_w,
        22.0F * layout.dpi_scale
    };

    layout.edition_bounds = Rect{
        title_x,
        layout.headline_bounds.bottom() + 2.0F * layout.dpi_scale,
        title_w,
        18.0F * layout.dpi_scale
    };

    layout.hero_panel_bounds = layout.base_layout.dialog_bounds;
    layout.brand_bounds = layout.headline_bounds;
    layout.platform_badge_bounds = layout.edition_bounds;
    layout.credits_label_bounds = Rect{modal_x, modal_y, 0, 0};
    layout.credits_name_bounds = Rect{modal_x, modal_y, 0, 0};

    // Specs rows (clean list starting below separator)
    const float specs_start_y = modal_y + 82.0F * layout.dpi_scale;
    const float specs_w = modal_w - 48.0F * layout.dpi_scale;
    layout.specs_panel_bounds = Rect{
        modal_x + 24.0F * layout.dpi_scale,
        specs_start_y,
        specs_w,
        modal_h - 140.0F * layout.dpi_scale
    };

    const float row_height = 21.0F * layout.dpi_scale;
    layout.spec_row_bounds.resize(m_specs.size());
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        layout.spec_row_bounds[i] = Rect{
            modal_x + 24.0F * layout.dpi_scale,
            specs_start_y + static_cast<float>(i) * row_height,
            specs_w,
            row_height
        };
    }

    // Action buttons in bottom-right
    const float btn_h = 28.0F * layout.dpi_scale;
    const float btn_w = 70.0F * layout.dpi_scale;
    const float btn_y = modal_y + modal_h - btn_h - 16.0F * layout.dpi_scale;

    layout.ok_button_bounds = Rect{
        modal_x + modal_w - btn_w - 20.0F * layout.dpi_scale,
        btn_y,
        btn_w,
        btn_h
    };

    layout.copy_button_bounds = Rect{
        layout.ok_button_bounds.x - btn_w - 10.0F * layout.dpi_scale,
        btn_y,
        btn_w,
        btn_h
    };

    return layout;
}

bool AboutModal::handle_pointer_press(float x, float y, const AboutModalLayoutResult& layout) noexcept {
    if (!m_visible) {
        return false;
    }

    if (layout.is_close_button(x, y)) {
        m_close_pressed = true;
        return true;
    }

    if (layout.is_copy_button(x, y)) {
        m_copy_pressed = true;
        return true;
    }

    if (layout.is_ok_button(x, y)) {
        m_ok_pressed = true;
        return true;
    }

    if (layout.is_inside_backdrop(x, y)) {
        if (m_backdrop_config.dismiss_on_backdrop_click) {
            m_backdrop_pressed = true;
            return true;
        }
    }

    return layout.is_inside_dialog(x, y);
}

bool AboutModal::handle_pointer_move(float x, float y, const AboutModalLayoutResult& layout) noexcept {
    if (!m_visible) {
        return false;
    }

    const bool was_close_h = m_close_hovered;
    const bool was_copy_h = m_copy_hovered;
    const bool was_ok_h = m_ok_hovered;

    m_close_hovered = layout.is_close_button(x, y);
    m_copy_hovered = layout.is_copy_button(x, y);
    m_ok_hovered = layout.is_ok_button(x, y);

    return (was_close_h != m_close_hovered) || (was_copy_h != m_copy_hovered) || (was_ok_h != m_ok_hovered);
}

bool AboutModal::handle_pointer_release(float x, float y, const AboutModalLayoutResult& layout, const std::function<void(const std::string&)>& copy_callback) noexcept {
    if (!m_visible) {
        return false;
    }

    bool handled = false;

    if (m_close_pressed) {
        m_close_pressed = false;
        if (layout.is_close_button(x, y)) {
            close();
            return true;
        }
        handled = true;
    }

    if (m_ok_pressed) {
        m_ok_pressed = false;
        if (layout.is_ok_button(x, y)) {
            close();
            return true;
        }
        handled = true;
    }

    if (m_copy_pressed) {
        m_copy_pressed = false;
        if (layout.is_copy_button(x, y)) {
            if (copy_callback) {
                copy_callback(get_clipboard_text());
            }
            return true;
        }
        handled = true;
    }

    if (m_backdrop_pressed) {
        m_backdrop_pressed = false;
        if (m_backdrop_config.dismiss_on_backdrop_click && layout.is_inside_backdrop(x, y)) {
            close();
            return true;
        }
        handled = true;
    }

    return handled || layout.is_inside_dialog(x, y);
}

bool AboutModal::handle_escape() noexcept {
    if (m_visible) {
        close();
        return true;
    }
    return false;
}

bool AboutModal::handle_enter() noexcept {
    if (m_visible) {
        close();
        return true;
    }
    return false;
}

} // namespace Zenvra::UI::Components
