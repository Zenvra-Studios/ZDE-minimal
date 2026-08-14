#include "UI/Components/Modal.h"

#include <algorithm>
#include <utility>

namespace Zenvra::UI::Components {

Modal::Modal()
    : m_theme(ModalTheme::from_theme(Theme::StudioTheme::zenvra_dark()))
{
}

Modal::Modal(std::string title, std::string message)
    : m_title(std::move(title))
    , m_message(std::move(message))
    , m_theme(ModalTheme::from_theme(Theme::StudioTheme::zenvra_dark()))
{
}

void Modal::close() noexcept {
    if (m_visible) {
        m_visible = false;
        m_close_hovered = false;
        m_close_pressed = false;
        m_backdrop_pressed = false;
        m_pressed_button_index = std::nullopt;
        if (m_on_close) {
            m_on_close();
        }
    }
}

void Modal::set_primary_button(std::string label, std::function<void()> on_click) {
    auto it = std::find_if(m_buttons.begin(), m_buttons.end(), [](const ModalButtonConfig& btn) {
        return btn.is_primary;
    });

    if (it != m_buttons.end()) {
        it->label = std::move(label);
        it->on_click = std::move(on_click);
    } else {
        m_buttons.push_back(ModalButtonConfig{
            .label = std::move(label),
            .is_primary = true,
            .on_click = std::move(on_click),
            .hovered = false,
            .pressed = false
        });
    }
}

void Modal::set_secondary_button(std::string label, std::function<void()> on_click) {
    auto it = std::find_if(m_buttons.begin(), m_buttons.end(), [](const ModalButtonConfig& btn) {
        return !btn.is_primary;
    });

    if (it != m_buttons.end()) {
        it->label = std::move(label);
        it->on_click = std::move(on_click);
    } else {
        // Insert secondary button before primary button if possible
        auto prim_it = std::find_if(m_buttons.begin(), m_buttons.end(), [](const ModalButtonConfig& btn) {
            return btn.is_primary;
        });
        m_buttons.insert(prim_it, ModalButtonConfig{
            .label = std::move(label),
            .is_primary = false,
            .on_click = std::move(on_click),
            .hovered = false,
            .pressed = false
        });
    }
}

void Modal::add_button(ModalButtonConfig button) {
    m_buttons.push_back(std::move(button));
}

void Modal::clear_buttons() noexcept {
    m_buttons.clear();
}

ModalLayoutResult Modal::calculate_layout(const Rect& viewport_bounds, float dpi_scale) const noexcept {
    ModalLayoutResult layout{};
    layout.dpi_scale = std::max(dpi_scale, 0.5F);
    layout.backdrop_bounds = viewport_bounds;

    const float scaled_padding = m_metrics.padding * layout.dpi_scale;
    const float raw_width = m_metrics.width * layout.dpi_scale;
    const float raw_height = m_metrics.height * layout.dpi_scale;

    const float dialog_width = std::max(100.0F * layout.dpi_scale,
        std::min(raw_width, std::max(0.0F, viewport_bounds.width - 2.0F * scaled_padding)));
    const float dialog_height = std::max(80.0F * layout.dpi_scale,
        std::min(raw_height, std::max(0.0F, viewport_bounds.height - 2.0F * scaled_padding)));

    const float dialog_x = viewport_bounds.x + (viewport_bounds.width - dialog_width) * 0.5F;
    const float dialog_y = viewport_bounds.y + (viewport_bounds.height - dialog_height) * 0.5F;
    layout.dialog_bounds = Rect{dialog_x, dialog_y, dialog_width, dialog_height};

    // Header layout
    const float header_h = m_metrics.header_height * layout.dpi_scale;
    layout.header_bounds = Rect{dialog_x, dialog_y, dialog_width, header_h};

    const float close_size = m_metrics.close_button_size * layout.dpi_scale;
    const float close_padding = (header_h - close_size) * 0.5F;
    layout.close_button_bounds = Rect{
        dialog_x + dialog_width - close_size - close_padding,
        dialog_y + close_padding,
        close_size,
        close_size
    };

    layout.title_bounds = Rect{
        dialog_x + scaled_padding,
        dialog_y,
        std::max(0.0F, layout.close_button_bounds.x - dialog_x - 2.0F * scaled_padding),
        header_h
    };

    // Footer layout
    const float footer_h = m_metrics.footer_height * layout.dpi_scale;
    const float footer_y = dialog_y + dialog_height - footer_h;
    layout.footer_bounds = Rect{dialog_x, footer_y, dialog_width, footer_h};

    // Body content layout
    const float body_y = dialog_y + header_h;
    const float body_h = std::max(0.0F, footer_y - body_y);
    layout.body_bounds = Rect{
        dialog_x + scaled_padding,
        body_y + (scaled_padding * 0.5F),
        std::max(0.0F, dialog_width - 2.0F * scaled_padding),
        std::max(0.0F, body_h - scaled_padding)
    };

    // Buttons layout (Right-aligned inside footer)
    layout.button_bounds.resize(m_buttons.size());
    const float btn_height = m_metrics.button_height * layout.dpi_scale;
    const float btn_spacing = m_metrics.button_spacing * layout.dpi_scale;
    const float btn_min_w = m_metrics.button_min_width * layout.dpi_scale;
    const float btn_y = footer_y + (footer_h - btn_height) * 0.5F;

    float current_right = dialog_x + dialog_width - scaled_padding;
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_buttons.size()) - 1; i >= 0; --i) {
        const auto idx = static_cast<std::size_t>(i);
        // Estimate width based on label length (approx 8px per char)
        const float text_w = static_cast<float>(m_buttons[idx].label.length()) * 8.5F * layout.dpi_scale;
        const float btn_w = std::max(btn_min_w, text_w + 24.0F * layout.dpi_scale);

        current_right -= btn_w;
        layout.button_bounds[idx] = Rect{current_right, btn_y, btn_w, btn_height};
        current_right -= btn_spacing;
    }

    return layout;
}

bool Modal::handle_pointer_press(float x, float y, const ModalLayoutResult& layout) noexcept {
    if (!m_visible) {
        return false;
    }

    if (layout.is_close_button(x, y)) {
        m_close_pressed = true;
        return true;
    }

    const auto btn_idx = layout.get_button_index(x, y);
    if (btn_idx.has_value()) {
        m_pressed_button_index = btn_idx;
        return true;
    }

    if (layout.is_inside_backdrop(x, y)) {
        if (m_backdrop_config.dismiss_on_backdrop_click) {
            m_backdrop_pressed = true;
            return true;
        }
    }

    // Modal dialog itself absorbs clicks to prevent click-through
    return layout.is_inside_dialog(x, y);
}

bool Modal::handle_pointer_move(float x, float y, const ModalLayoutResult& layout) noexcept {
    if (!m_visible) {
        return false;
    }

    const bool was_close_hovered = m_close_hovered;
    m_close_hovered = layout.is_close_button(x, y);

    const auto hovered_btn = layout.get_button_index(x, y);
    bool buttons_changed = false;
    for (std::size_t i = 0; i < m_buttons.size(); ++i) {
        const bool is_hover = (hovered_btn.has_value() && *hovered_btn == i);
        if (m_buttons[i].hovered != is_hover) {
            m_buttons[i].hovered = is_hover;
            buttons_changed = true;
        }
    }

    return (was_close_hovered != m_close_hovered) || buttons_changed;
}

bool Modal::handle_pointer_release(float x, float y, const ModalLayoutResult& layout) noexcept {
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

    if (m_pressed_button_index.has_value()) {
        const std::size_t idx = *m_pressed_button_index;
        m_pressed_button_index = std::nullopt;
        if (idx < m_buttons.size() && layout.button_bounds[idx].contains(x, y)) {
            if (m_buttons[idx].on_click) {
                m_buttons[idx].on_click();
            } else {
                close();
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

bool Modal::handle_escape() noexcept {
    if (m_visible) {
        close();
        return true;
    }
    return false;
}

bool Modal::handle_enter() noexcept {
    if (!m_visible) {
        return false;
    }

    // Trigger primary button if present
    for (const auto& btn : m_buttons) {
        if (btn.is_primary) {
            if (btn.on_click) {
                btn.on_click();
            } else {
                close();
            }
            return true;
        }
    }

    return false;
}

} // namespace Zenvra::UI::Components
