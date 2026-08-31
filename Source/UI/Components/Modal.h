#pragma once

#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"
#include "Drivers/Graphics/BackdropBlurPipeline.h"
#include "Drivers/Graphics/effect/BackdropBlur.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zenvra::UI::Components {

/// Configuration for backdrop blur effect using BackdropBlur.h shaders.
struct ModalBackdropConfig {
    bool blur_enabled = true;
    float blur_radius = 24.0F;
    float saturation = 1.15F;
    float noise_opacity = 0.035F;
    Theme::Color tint = {15, 15, 18, 160};
    bool dismiss_on_backdrop_click = true;

    [[nodiscard]] static constexpr std::string_view get_vertex_shader() noexcept {
        return Graphics::Shaders::BlurVertexShader;
    }

    [[nodiscard]] static constexpr std::string_view get_fragment_shader() noexcept {
        return Graphics::Shaders::BlurFragmentShader;
    }

    [[nodiscard]] std::array<float, 4> get_normalized_tint() const noexcept {
        return {
            static_cast<float>(tint.red) / 255.0F,
            static_cast<float>(tint.green) / 255.0F,
            static_cast<float>(tint.blue) / 255.0F,
            static_cast<float>(tint.alpha) / 255.0F
        };
    }

    [[nodiscard]] Graphics::BlurUniforms to_blur_uniforms(float viewport_width, float viewport_height) const noexcept {
        Graphics::BlurUniforms uniforms{};
        uniforms.radius = blur_radius;
        uniforms.saturation = saturation;
        uniforms.tint = get_normalized_tint();
        uniforms.noise_opacity = noise_opacity;
        uniforms.texel_width = viewport_width > 0.0F ? (1.0F / viewport_width) : (1.0F / 1920.0F);
        uniforms.texel_height = viewport_height > 0.0F ? (1.0F / viewport_height) : (1.0F / 1080.0F);
        return uniforms;
    }
};

/// Solid theme configuration for the modal dialog body and its elements.
struct ModalTheme {
    Theme::Color dialog_background = {37, 37, 38, 255};    // Solid panel surface
    Theme::Color header_background = {29, 30, 33, 255};    // Solid title surface
    Theme::Color border_color = {48, 50, 55, 255};         // Solid border line
    Theme::Color text_primary = {204, 204, 204, 255};      // Main text
    Theme::Color text_secondary = {154, 154, 154, 255};    // Secondary / description text
    Theme::Color accent = {0, 122, 204, 255};              // Primary button background
    Theme::Color button_secondary = {45, 45, 45, 255};     // Secondary button background
    Theme::Color hover = {55, 55, 55, 255};                // Button hover
    Theme::Color pressed = {65, 65, 65, 255};              // Button pressed
    Theme::Color close_hover = {196, 43, 28, 255};         // Close button hover color

    [[nodiscard]] static ModalTheme from_theme(const Theme::StudioTheme& theme) noexcept {
        return ModalTheme{
            .dialog_background = theme.panel_background,
            .header_background = theme.titlebar_background,
            .border_color = theme.titlebar_border,
            .text_primary = theme.text_primary,
            .text_secondary = theme.text_secondary,
            .accent = theme.accent,
            .button_secondary = theme.hover,
            .hover = theme.pressed,
            .pressed = theme.command_center_border,
            .close_hover = theme.close_hover,
        };
    }
};

struct ModalButtonConfig {
    std::string label;
    bool is_primary = false;
    std::function<void()> on_click;
    bool hovered = false;
    bool pressed = false;
};

struct ModalMetrics {
    float width = 480.0F;
    float height = 240.0F;
    float header_height = 40.0F;
    float footer_height = 56.0F;
    float padding = 20.0F;
    float border_width = 1.0F;
    float corner_radius = 8.0F;
    float close_button_size = 28.0F;
    float button_height = 32.0F;
    float button_min_width = 84.0F;
    float button_spacing = 10.0F;
};

struct ModalLayoutResult {
    Rect backdrop_bounds;
    Rect dialog_bounds;
    Rect header_bounds;
    Rect title_bounds;
    Rect close_button_bounds;
    Rect body_bounds;
    Rect footer_bounds;
    std::vector<Rect> button_bounds;
    float dpi_scale = 1.0F;

    [[nodiscard]] bool is_inside_dialog(float x, float y) const noexcept {
        return dialog_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_inside_backdrop(float x, float y) const noexcept {
        return backdrop_bounds.contains(x, y) && !dialog_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_close_button(float x, float y) const noexcept {
        return close_button_bounds.contains(x, y);
    }

    [[nodiscard]] std::optional<std::size_t> get_button_index(float x, float y) const noexcept {
        for (std::size_t i = 0; i < button_bounds.size(); ++i) {
            if (button_bounds[i].contains(x, y)) {
                return i;
            }
        }
        return std::nullopt;
    }
};

/// UI Modal Dialog Component with backdrop blur integration and solid theme styling.
class Modal {
public:
    Modal();
    explicit Modal(std::string title, std::string message = "");

    // Visibility
    void open() noexcept { m_visible = true; }
    void close() noexcept;
    void set_visible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    // Content configuration
    void set_title(std::string title) { m_title = std::move(title); }
    [[nodiscard]] const std::string& get_title() const noexcept { return m_title; }

    void set_message(std::string message) { m_message = std::move(message); }
    [[nodiscard]] const std::string& get_message() const noexcept { return m_message; }

    // Theme & Styling (Solid)
    void set_theme(const ModalTheme& theme) noexcept { m_theme = theme; }
    [[nodiscard]] const ModalTheme& get_theme() const noexcept { return m_theme; }

    // Backdrop blur settings
    void set_backdrop_config(const ModalBackdropConfig& config) noexcept { m_backdrop_config = config; }
    [[nodiscard]] const ModalBackdropConfig& get_backdrop_config() const noexcept { return m_backdrop_config; }
    [[nodiscard]] ModalBackdropConfig& get_backdrop_config_mut() noexcept { return m_backdrop_config; }

    // Metrics
    void set_metrics(const ModalMetrics& metrics) noexcept { m_metrics = metrics; }
    [[nodiscard]] const ModalMetrics& get_metrics() const noexcept { return m_metrics; }

    // Button management
    void set_primary_button(std::string label, std::function<void()> on_click = nullptr);
    void set_secondary_button(std::string label, std::function<void()> on_click = nullptr);
    void add_button(ModalButtonConfig button);
    void clear_buttons() noexcept;
    [[nodiscard]] const std::vector<ModalButtonConfig>& get_buttons() const noexcept { return m_buttons; }

    // Callbacks
    void set_on_close(std::function<void()> on_close) { m_on_close = std::move(on_close); }

    // Layout calculation
    [[nodiscard]] ModalLayoutResult calculate_layout(const Rect& viewport_bounds, float dpi_scale = 1.0F) const noexcept;

    // Interactive event handling
    [[nodiscard]] bool handle_pointer_press(float x, float y, const ModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_move(float x, float y, const ModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_release(float x, float y, const ModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_escape() noexcept;
    [[nodiscard]] bool handle_enter() noexcept;

    // Interactive state getters
    [[nodiscard]] bool is_close_button_hovered() const noexcept { return m_close_hovered; }
    [[nodiscard]] bool is_close_button_pressed() const noexcept { return m_close_pressed; }

private:
    std::string m_title;
    std::string m_message;
    ModalTheme m_theme;
    ModalBackdropConfig m_backdrop_config;
    ModalMetrics m_metrics;
    std::vector<ModalButtonConfig> m_buttons;
    std::function<void()> m_on_close;

    bool m_visible = false;
    bool m_close_hovered = false;
    bool m_close_pressed = false;
    bool m_backdrop_pressed = false;
    std::optional<std::size_t> m_pressed_button_index;
};

} // namespace Zenvra::UI::Components
