#pragma once

#include "Platform/IPlatformWindow.h"
#include "Platform/X11/Components/FileDropTarget.h"
#include "Platform/X11/Components/X11ChromeRenderer.h"
#include "Platform/X11/Components/X11PromptDialog.h"
#include "Platform/X11/Components/X11AddNewItemDialog.h"
#include "UI/Chrome/WindowChromeLayout.h"
#include "UI/Components/AboutModal.h"
#include "UI/Theme/StudioTheme.h"
#include "Language/Protocol/LspTypes.h"

#include <X11/Xlib.h>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace Zenvra::Platform::X11
{

class X11Window final : public IPlatformWindow
{
public:
    explicit X11Window(const WindowSpecification& specification);
    ~X11Window() override;

    X11Window(const X11Window&) = delete;
    X11Window& operator=(const X11Window&) = delete;

    [[nodiscard]] bool initialize() override;
    void show() override;
    void poll_events() override;
    [[nodiscard]] bool should_close() const override;

    void minimize() override;
    void maximize() override;
    void restore() override;
    void request_close() override;

    [[nodiscard]] bool is_maximized() const override;
    [[nodiscard]] bool is_minimized() const override;
    [[nodiscard]] bool is_focused() const override;
    [[nodiscard]] const WindowCapabilities& get_capabilities() const noexcept override;
    [[nodiscard]] void* get_native_handle() const noexcept override;

    void set_custom_chrome_enabled(bool enabled) override;
    void set_titlebar_hit_test_callback(TitlebarHitTestCallback callback) override;
    void set_command_invoked_callback(CommandInvokedCallback callback) override;
    void set_command_state_query_callback(CommandStateQueryCallback callback) override;

    [[nodiscard]] bool open_project_folder() override;
    [[nodiscard]] bool close_project() override;
    void toggle_terminal() override;
    void toggle_shader_sandbox() override;
    void show_about_dialog() override;
    [[nodiscard]] bool is_modal_active() const override;
    void toggle_fullscreen() override;
    [[nodiscard]] bool is_fullscreen() const override;
    void reset_layout() override;

private:
    struct Atoms
    {
        Atom wm_protocols = None;
        Atom wm_delete_window = None;
        Atom utf8_string = None;
        Atom net_wm_name = None;
        Atom net_wm_icon = None;
        Atom net_wm_pid = None;
        Atom net_wm_window_type = None;
        Atom net_wm_window_type_normal = None;
        Atom net_supported = None;
        Atom net_workarea = None;
        Atom net_current_desktop = None;
        Atom net_wm_state = None;
        Atom net_wm_state_maximized_horizontal = None;
        Atom net_wm_state_maximized_vertical = None;
        Atom net_wm_state_hidden = None;
        Atom net_wm_move_resize = None;
        Atom motif_wm_hints = None;
    };

    enum class MoveResizeDirection : long
    {
        SizeTopLeft = 0,
        SizeTop = 1,
        SizeTopRight = 2,
        SizeRight = 3,
        SizeBottomRight = 4,
        SizeBottom = 5,
        SizeBottomLeft = 6,
        SizeLeft = 7,
        Move = 8,
        Cancel = 11,
    };

    struct WorkArea
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    void initialize_atoms();
    void initialize_cursors();
    void release_native_resources();
    void apply_window_icon() const;
    void apply_custom_chrome();
    void apply_size_hints() const;
    void refresh_chrome_layout();
    void refresh_window_state();
    void render(std::optional<UI::Rect> dirty_rect = std::nullopt);
    void handle_event(XEvent& event);
    void handle_motion(const XMotionEvent& event);
    void handle_button_press(const XButtonEvent& event);
    void handle_button_release(const XButtonEvent& event);
    void handle_key_press(XKeyEvent& event);
    void update_cursor(int point_x, int point_y);
    void begin_move_resize(const XButtonEvent& event, MoveResizeDirection direction);
    void update_manual_move_resize(const XMotionEvent& event);
    void end_manual_move_resize(Time event_time);
    void send_maximized_state(long operation);
    void open_menu(std::size_t menu_index, bool select_first_item, const UI::Rect* anchor_override = nullptr);
    void move_popup_selection(int direction);
    void execute_popup_selection();
    void show_explorer_context_menu(const std::filesystem::path& target_path, int client_x, int client_y);
    void show_editor_context_menu(int client_x, int client_y);
    void execute_explorer_command(std::string_view command);
    void copy_to_clipboard(const std::string& text);
    void discard_pointer_events();

    [[nodiscard]] float calculate_dpi_scale() const;
    [[nodiscard]] WorkArea get_work_area() const;
    [[nodiscard]] bool is_drag_region(float point_x, float point_y) const;
    [[nodiscard]] std::optional<MoveResizeDirection> get_resize_direction(int point_x, int point_y) const;
    [[nodiscard]] std::optional<std::size_t> get_popup_item_index(int point_x, int point_y) const;
    [[nodiscard]] std::optional<std::size_t> get_overflow_popup_menu_index(
        int point_x,
        int point_y) const;
    [[nodiscard]] bool is_popup_item_enabled(std::size_t menu_index, std::size_t item_index) const;
    [[nodiscard]] bool is_root_atom_supported(Atom atom) const;

    Display* m_display = nullptr;
    int m_screen = 0;
    Window m_window_handle = 0;
    WindowSpecification m_specification;
    WindowCapabilities m_capabilities;
    Atoms m_atoms;
    int m_client_width = 0;
    int m_client_height = 0;
    float m_dpi_scale = 1.0F;
    bool m_should_close = false;
    bool m_is_maximized = false;
    bool m_is_minimized = false;
    bool m_is_focused = false;
    bool m_custom_chrome_enabled = false;
    bool m_context_acquired = false;
    bool m_ewmh_move_resize_supported = false;
    bool m_ewmh_maximize_supported = false;
    bool m_menu_pointer_tracking = false;
    Time m_last_titlebar_click_time = 0;
    int m_last_titlebar_click_x = 0;
    int m_last_titlebar_click_y = 0;
    Time m_last_workspace_click_time = 0;
    int m_last_workspace_click_x = 0;
    int m_last_workspace_click_y = 0;
    int m_workspace_click_count = 0;
    Cursor m_default_cursor = None;
    Cursor m_pointer_cursor = None;
    Cursor m_text_cursor = None;
    Cursor m_split_resize_cursor = None;
    Cursor m_horizontal_split_resize_cursor = None;
    Cursor m_active_cursor = None;
    std::array<Cursor, 9> m_move_resize_cursors{};
    TitlebarHitTestCallback m_titlebar_hit_test_callback;
    CommandInvokedCallback m_command_invoked_callback;
    CommandStateQueryCallback m_command_state_query_callback;
    UI::Theme::StudioTheme m_theme = UI::Theme::StudioTheme::zenvra_dark();
    UI::Chrome::WindowChromeLayout m_chrome_layout_engine;
    UI::Chrome::WindowChromeLayoutResult m_chrome_layout;
    Components::X11ChromeRenderer m_chrome_renderer;
    Components::FileDropTarget m_file_drop_target;
    Components::ChromeInteractionState m_interaction_state;
    std::optional<std::size_t> m_pressed_popup_item_index;
    std::optional<MoveResizeDirection> m_manual_move_resize_direction;
    int m_manual_start_root_x = 0;
    int m_manual_start_root_y = 0;
    int m_manual_start_window_x = 0;
    int m_manual_start_window_y = 0;
    int m_manual_start_width = 0;
    int m_manual_start_height = 0;
    int m_restore_x = 0;
    int m_restore_y = 0;
    int m_restore_width = 0;
    int m_restore_height = 0;
    bool m_restore_bounds_valid = false;
    std::filesystem::path m_context_menu_target_path;
    std::string m_clipboard_text;
    Components::X11PromptDialog m_prompt_dialog;
    Components::X11AddNewItemDialog m_add_item_dialog;
    UI::Components::AboutModal m_about_modal;
    bool m_is_fullscreen = false;
    std::chrono::steady_clock::time_point m_last_animation_frame_time = std::chrono::steady_clock::now();

    // Thread-safe diagnostics queue: LSP thread writes, main thread reads
    struct PendingDiagnostics {
        std::string uri;
        std::vector<Language::Protocol::Diagnostic> diagnostics;
    };
    std::mutex m_pending_diag_mutex;
    std::vector<PendingDiagnostics> m_pending_diagnostics;
    std::atomic<bool> m_has_pending_diagnostics{false};
    void flush_pending_diagnostics();

    void draw_about_modal(Drawable drawable, int client_width, int client_height);
};

} // namespace Zenvra::Platform::X11
