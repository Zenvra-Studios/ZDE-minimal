#include "UI/Components/PromptModal.h"

#include <algorithm>

namespace Zenvra::UI::Components {

PromptModal::PromptModal()
{
}

void PromptModal::open_new_file(const std::filesystem::path& target_dir, std::function<void(const std::string&)> on_confirm)
{
    m_mode = PromptMode::NewFile;
    m_title = "New File";
    m_target_path = target_dir;
    m_subtitle = "Target: " + target_dir.filename().string() + "/";
    m_confirm_label = "Create";
    m_input.set_text("");
    m_input.set_placeholder("File name (e.g. main.rs, components/button.cpp)");
    m_input.set_focused(true);
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;
    m_visible = true;
}

void PromptModal::open_new_folder(const std::filesystem::path& target_dir, std::function<void(const std::string&)> on_confirm)
{
    m_mode = PromptMode::NewFolder;
    m_title = "New Folder";
    m_target_path = target_dir;
    m_subtitle = "Target: " + target_dir.filename().string() + "/";
    m_confirm_label = "Create";
    m_input.set_text("");
    m_input.set_placeholder("Folder name (e.g. models, include/core)");
    m_input.set_focused(true);
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;
    m_visible = true;
}

void PromptModal::open_rename(const std::filesystem::path& item_path, std::function<void(const std::string&)> on_confirm)
{
    m_mode = PromptMode::Rename;
    m_title = "Rename Item";
    m_target_path = item_path;
    m_subtitle = "Rename: " + item_path.filename().string();
    m_confirm_label = "Rename";
    m_input.set_text(item_path.filename().string());
    m_input.set_placeholder("New name");
    m_input.set_focused(true);
    m_on_confirm_string = std::move(on_confirm);
    m_on_confirm_void = nullptr;
    m_visible = true;
}

void PromptModal::open_delete(const std::filesystem::path& item_path, std::function<void()> on_confirm)
{
    m_mode = PromptMode::ConfirmDelete;
    m_title = "Delete Item";
    m_target_path = item_path;
    m_subtitle = "Are you sure you want to permanently delete '" + item_path.filename().string() + "'?";
    m_confirm_label = "Delete";
    m_input.set_text("");
    m_input.set_placeholder("");
    m_input.set_focused(false);
    m_on_confirm_string = nullptr;
    m_on_confirm_void = std::move(on_confirm);
    m_visible = true;
}

void PromptModal::close() noexcept
{
    m_visible = false;
    m_close_hovered = false;
    m_close_pressed = false;
    m_ok_hovered = false;
    m_ok_pressed = false;
    m_cancel_hovered = false;
    m_cancel_pressed = false;
}

void PromptModal::submit()
{
    if (m_mode == PromptMode::ConfirmDelete)
    {
        if (m_on_confirm_void)
        {
            m_on_confirm_void();
        }
        close();
    }
    else
    {
        const std::string text = m_input.get_text();
        if (!text.empty())
        {
            if (m_on_confirm_string)
            {
                m_on_confirm_string(text);
            }
            close();
        }
    }
}

PromptModalLayoutResult PromptModal::calculate_layout(const Rect& viewport_bounds, float dpi_scale) const noexcept
{
    PromptModalLayoutResult layout{};
    layout.dpi_scale = dpi_scale;

    const float dialog_w = std::min(460.0F * dpi_scale, viewport_bounds.width - 32.0F * dpi_scale);
    const float dialog_h = (m_mode == PromptMode::ConfirmDelete) ? (160.0F * dpi_scale) : (190.0F * dpi_scale);
    const float dialog_x = viewport_bounds.x + (viewport_bounds.width - dialog_w) * 0.5F;
    const float dialog_y = viewport_bounds.y + (viewport_bounds.height - dialog_h) * 0.35F;

    layout.base_layout.dialog_bounds = Rect{dialog_x, dialog_y, dialog_w, dialog_h};
    layout.base_layout.backdrop_bounds = viewport_bounds;

    const float pad = 18.0F * dpi_scale;
    const float close_size = 20.0F * dpi_scale;
    layout.close_button_bounds = Rect{
        dialog_x + dialog_w - pad - close_size,
        dialog_y + 14.0F * dpi_scale,
        close_size,
        close_size
    };

    layout.title_bounds = Rect{
        dialog_x + pad,
        dialog_y + 14.0F * dpi_scale,
        dialog_w - pad * 2.0F - close_size - 8.0F * dpi_scale,
        22.0F * dpi_scale
    };

    layout.subtitle_bounds = Rect{
        dialog_x + pad,
        layout.title_bounds.bottom() + 4.0F * dpi_scale,
        dialog_w - pad * 2.0F,
        18.0F * dpi_scale
    };

    if (m_mode != PromptMode::ConfirmDelete)
    {
        layout.input_bounds = Rect{
            dialog_x + pad,
            layout.subtitle_bounds.bottom() + 10.0F * dpi_scale,
            dialog_w - pad * 2.0F,
            30.0F * dpi_scale
        };
    }

    const float btn_w = 90.0F * dpi_scale;
    const float btn_h = 28.0F * dpi_scale;
    const float btn_y = dialog_y + dialog_h - pad - btn_h;

    layout.ok_button_bounds = Rect{
        dialog_x + dialog_w - pad - btn_w,
        btn_y,
        btn_w,
        btn_h
    };

    layout.cancel_button_bounds = Rect{
        layout.ok_button_bounds.x - btn_w - 8.0F * dpi_scale,
        btn_y,
        btn_w,
        btn_h
    };

    return layout;
}

bool PromptModal::handle_pointer_press(float x, float y, const PromptModalLayoutResult& layout) noexcept
{
    if (!m_visible) return false;

    if (layout.is_close_button(x, y))
    {
        m_close_pressed = true;
        return true;
    }
    if (layout.is_ok_button(x, y))
    {
        m_ok_pressed = true;
        return true;
    }
    if (layout.is_cancel_button(x, y))
    {
        m_cancel_pressed = true;
        return true;
    }
    if (layout.is_input(x, y))
    {
        m_input.set_focused(true);
        return true;
    }

    if (!layout.is_inside_dialog(x, y))
    {
        close();
        return true;
    }

    return true;
}

bool PromptModal::handle_pointer_move(float x, float y, const PromptModalLayoutResult& layout) noexcept
{
    if (!m_visible) return false;

    const bool close_h = layout.is_close_button(x, y);
    const bool ok_h = layout.is_ok_button(x, y);
    const bool cancel_h = layout.is_cancel_button(x, y);

    const bool changed = (close_h != m_close_hovered) || (ok_h != m_ok_hovered) || (cancel_h != m_cancel_hovered);
    m_close_hovered = close_h;
    m_ok_hovered = ok_h;
    m_cancel_hovered = cancel_h;

    return changed;
}

bool PromptModal::handle_pointer_release(float x, float y, const PromptModalLayoutResult& layout) noexcept
{
    if (!m_visible) return false;

    if (m_close_pressed && layout.is_close_button(x, y))
    {
        close();
        return true;
    }
    if (m_ok_pressed && layout.is_ok_button(x, y))
    {
        submit();
        return true;
    }
    if (m_cancel_pressed && layout.is_cancel_button(x, y))
    {
        close();
        return true;
    }

    m_close_pressed = false;
    m_ok_pressed = false;
    m_cancel_pressed = false;
    return true;
}

bool PromptModal::handle_char(char32_t codepoint) noexcept
{
    if (!m_visible || m_mode == PromptMode::ConfirmDelete) return false;

    if (codepoint >= 32)
    {
        std::string utf8_char;
        if (codepoint <= 0x7F)
        {
            utf8_char += static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
            utf8_char += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
            utf8_char += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else
        {
            utf8_char += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
            utf8_char += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8_char += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        return m_input.handle_text_input(utf8_char);
    }
    return false;
}

bool PromptModal::handle_backspace() noexcept
{
    if (!m_visible || m_mode == PromptMode::ConfirmDelete) return false;
    return m_input.handle_backspace();
}

bool PromptModal::handle_escape() noexcept
{
    if (!m_visible) return false;
    close();
    return true;
}

bool PromptModal::handle_enter() noexcept
{
    if (!m_visible) return false;
    submit();
    return true;
}

} // namespace Zenvra::UI::Components
