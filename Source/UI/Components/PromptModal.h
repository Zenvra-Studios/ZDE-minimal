#pragma once

#include "UI/Components/Input.h"
#include "UI/Components/Modal.h"
#include "UI/Geometry.h"
#include "UI/Theme/StudioTheme.h"

#include <filesystem>
#include <functional>
#include <string>

namespace Zenvra::UI::Components {

enum class PromptMode {
    NewFile,
    NewFolder,
    Rename,
    ConfirmDelete
};

struct PromptModalLayoutResult {
    ModalLayoutResult base_layout;
    Rect title_bounds;
    Rect subtitle_bounds;
    Rect input_bounds;
    Rect ok_button_bounds;
    Rect cancel_button_bounds;
    Rect close_button_bounds;
    float dpi_scale = 1.0F;

    [[nodiscard]] bool is_inside_dialog(float x, float y) const noexcept {
        return base_layout.is_inside_dialog(x, y);
    }

    [[nodiscard]] bool is_inside_backdrop(float x, float y) const noexcept {
        return base_layout.is_inside_backdrop(x, y);
    }

    [[nodiscard]] bool is_input(float x, float y) const noexcept {
        return input_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_ok_button(float x, float y) const noexcept {
        return ok_button_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_cancel_button(float x, float y) const noexcept {
        return cancel_button_bounds.contains(x, y);
    }

    [[nodiscard]] bool is_close_button(float x, float y) const noexcept {
        return close_button_bounds.contains(x, y);
    }
};

class PromptModal {
public:
    PromptModal();

    void open_new_file(const std::filesystem::path& target_dir, std::function<void(const std::string&)> on_confirm);
    void open_new_folder(const std::filesystem::path& target_dir, std::function<void(const std::string&)> on_confirm);
    void open_rename(const std::filesystem::path& item_path, std::function<void(const std::string&)> on_confirm);
    void open_delete(const std::filesystem::path& item_path, std::function<void()> on_confirm);

    void close() noexcept;
    void set_visible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] bool is_visible() const noexcept { return m_visible; }

    [[nodiscard]] PromptMode get_mode() const noexcept { return m_mode; }
    [[nodiscard]] const std::string& get_title() const noexcept { return m_title; }
    [[nodiscard]] const std::string& get_subtitle() const noexcept { return m_subtitle; }
    [[nodiscard]] const std::string& get_confirm_label() const noexcept { return m_confirm_label; }
    [[nodiscard]] const Input& get_input() const noexcept { return m_input; }
    [[nodiscard]] Input& get_input_mut() noexcept { return m_input; }

    [[nodiscard]] PromptModalLayoutResult calculate_layout(const Rect& viewport_bounds, float dpi_scale = 1.0F) const noexcept;

    [[nodiscard]] bool handle_pointer_press(float x, float y, const PromptModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_move(float x, float y, const PromptModalLayoutResult& layout) noexcept;
    [[nodiscard]] bool handle_pointer_release(float x, float y, const PromptModalLayoutResult& layout) noexcept;

    [[nodiscard]] bool handle_char(char32_t codepoint) noexcept;
    [[nodiscard]] bool handle_backspace() noexcept;
    [[nodiscard]] bool handle_escape() noexcept;
    [[nodiscard]] bool handle_enter() noexcept;

    [[nodiscard]] bool is_close_hovered() const noexcept { return m_close_hovered; }
    [[nodiscard]] bool is_ok_hovered() const noexcept { return m_ok_hovered; }
    [[nodiscard]] bool is_cancel_hovered() const noexcept { return m_cancel_hovered; }
    [[nodiscard]] bool is_ok_pressed() const noexcept { return m_ok_pressed; }
    [[nodiscard]] bool is_cancel_pressed() const noexcept { return m_cancel_pressed; }
    [[nodiscard]] bool is_close_pressed() const noexcept { return m_close_pressed; }

private:
    void submit();

    PromptMode m_mode = PromptMode::NewFile;
    std::string m_title = "New File";
    std::string m_subtitle;
    std::string m_confirm_label = "Create";
    std::filesystem::path m_target_path;

    Input m_input;
    std::function<void(const std::string&)> m_on_confirm_string;
    std::function<void()> m_on_confirm_void;

    bool m_visible = false;
    bool m_close_hovered = false;
    bool m_close_pressed = false;
    bool m_ok_hovered = false;
    bool m_ok_pressed = false;
    bool m_cancel_hovered = false;
    bool m_cancel_pressed = false;
};

} // namespace Zenvra::UI::Components
