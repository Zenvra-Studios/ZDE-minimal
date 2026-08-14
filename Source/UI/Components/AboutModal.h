#pragma once

#include "UI/Components/Modal.h"
#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <string>
#include <utility>
#include <vector>

namespace Zenvra::UI::Components {

struct AboutSpecItem {
    std::string key;
    std::string value;
};

struct AboutModalLayoutResult {
    ModalLayoutResult base_layout;
    Rect hero_panel_bounds;
    Rect logo_bounds;
    Rect brand_bounds;
    Rect credits_label_bounds;
    Rect credits_name_bounds;
    Rect specs_panel_bounds;
    Rect headline_bounds;
    Rect edition_bounds;
    Rect platform_badge_bounds;
    std::vector<Rect> spec_row_bounds;
    Rect copy_button_bounds;
    Rect ok_button_bounds;
    Rect close_button_bounds;
    float dpi_scale = 1.0F;

    [[nodiscard]] bool is_inside_dialog(float x, float y) const noexcept {
        return base_layout.is_inside_dialog(x, y);
    }

    [[nodiscard]] bool is_inside_backdrop(float x, float y) const noexcept {
        return base_layout.is_inside_backdrop(x, y);
    }

    [[nodiscard]] bool is_copy_button(float x, float y) const noexcept {
        return copy_button_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_ok_button(float x, float y) const noexcept {
        return ok_button_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_close_button(float x, float y) const noexcept {
        return close_button_bounds.contains(x, y);
    }
};

/// Customized FL Studio-inspired About Modal Dialog with detailed tech specs.
class AboutModal {
public:
    AboutModal();

    // Visibility
    void open() noexcept { m_visible = true; }
    void close() noexcept;
    void set_visible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    // Theme & Backdrop
    void set_theme(const ModalTheme& theme) noexcept { m_theme = theme; }
    [[nodiscard]] const ModalTheme& get_theme() const noexcept { return m_theme; }

    void set_backdrop_config(const ModalBackdropConfig& config) noexcept { m_backdrop_config = config; }
    [[nodiscard]] const ModalBackdropConfig& get_backdrop_config() const noexcept { return m_backdrop_config; }

    // Metadata
    [[nodiscard]] const std::string& get_app_name() const noexcept { return m_app_name; }
    [[nodiscard]] const std::string& get_edition() const noexcept { return m_edition; }
    [[nodiscard]] const std::string& get_platform() const noexcept { return m_platform; }
    [[nodiscard]] const std::string& get_studio_name() const noexcept { return m_studio_name; }
    [[nodiscard]] const std::string& get_created_by() const noexcept { return m_created_by; }
    [[nodiscard]] const std::vector<AboutSpecItem>& get_specs() const noexcept { return m_specs; }

    /// Formats a complete multi-line specifications text for copying to the clipboard.
    [[nodiscard]] std::string get_clipboard_text() const;

    /// Calculates responsive layout metrics for the 2-column hero modal dialog.
    [[nodiscard]] AboutModalLayoutResult calculate_layout(const Rect& viewport_bounds, float dpi_scale = 1.0F) const noexcept;

    // Pointer events
    [[nodiscard]] bool handle_pointer_press(float x, float y, const AboutModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_move(float x, float y, const AboutModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_release(float x, float y, const AboutModalLayoutResult& layout, const std::function<void(const std::string&)>& copy_callback = nullptr) noexcept;

    // Keyboard events
    [[nodiscard]] bool handle_escape() noexcept;
    [[nodiscard]] bool handle_enter() noexcept;

    // Hover / pressed queries
    [[nodiscard]] bool is_close_hovered() const noexcept { return m_close_hovered; }
    [[nodiscard]] bool is_copy_hovered() const noexcept { return m_copy_hovered; }
    [[nodiscard]] bool is_ok_hovered() const noexcept { return m_ok_hovered; }
    [[nodiscard]] bool is_copy_pressed() const noexcept { return m_copy_pressed; }
    [[nodiscard]] bool is_ok_pressed() const noexcept { return m_ok_pressed; }
    [[nodiscard]] bool is_close_pressed() const noexcept { return m_close_pressed; }

private:
    std::string m_app_name = "ZDE STUDIO 2026";
    std::string m_edition = "Community & Pro Edition v0.1.0 [Build 2026.08]";
    std::string m_platform = "Windows - 64Bit";
    std::string m_studio_name = "Zenvra Studios";
    std::string m_created_by = "Zenvra Core Team (Zenvra Studios)";
    std::vector<AboutSpecItem> m_specs;

    ModalTheme m_theme;
    ModalBackdropConfig m_backdrop_config;

    bool m_visible = false;
    bool m_close_hovered = false;
    bool m_close_pressed = false;
    bool m_copy_hovered = false;
    bool m_copy_pressed = false;
    bool m_ok_hovered = false;
    bool m_ok_pressed = false;
    bool m_backdrop_pressed = false;
};

} // namespace Zenvra::UI::Components
