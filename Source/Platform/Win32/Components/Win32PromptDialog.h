#pragma once

#include <windows.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Components
{

enum class PromptDialogMode
{
    NewFile,
    NewFolder,
    Rename,
    ConfirmDelete
};

class Win32PromptDialog
{
public:
    Win32PromptDialog();
    ~Win32PromptDialog();

    Win32PromptDialog(const Win32PromptDialog&) = delete;
    Win32PromptDialog& operator=(const Win32PromptDialog&) = delete;

    bool open_new_file(
        HWND parent,
        const std::filesystem::path& target_dir,
        std::function<void(const std::string&)> on_confirm);

    bool open_new_folder(
        HWND parent,
        const std::filesystem::path& target_dir,
        std::function<void(const std::string&)> on_confirm);

    bool open_rename(
        HWND parent,
        const std::filesystem::path& item_path,
        std::function<void(const std::string&)> on_confirm);

    bool open_delete(
        HWND parent,
        const std::filesystem::path& item_path,
        std::function<void()> on_confirm);

    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT handle_message(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

    void refresh_fonts();
    void submit();

    HWND m_hwnd = nullptr;
    HWND m_parent_hwnd = nullptr;
    UINT m_dpi = 96;

    PromptDialogMode m_mode = PromptDialogMode::NewFolder;
    std::string m_title;
    std::string m_subtitle;
    std::string m_placeholder;
    std::string m_confirm_label;
    std::string m_text_value;
    std::size_t m_caret_pos = 0;
    bool m_caret_visible = true;
    bool m_ready_to_close = false;

    std::filesystem::path m_target_path;
    std::function<void(const std::string&)> m_on_confirm_string;
    std::function<void()> m_on_confirm_void;

    bool m_ok_hovered = false;
    bool m_cancel_hovered = false;

    HFONT m_title_font = nullptr;
    HFONT m_ui_font = nullptr;
    HFONT m_small_font = nullptr;
};

} // namespace Zenvra::Platform::Win32::Components
