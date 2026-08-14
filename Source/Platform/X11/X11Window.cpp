#include "Platform/X11/X11Window.h"
#include "Assets/Logo.h"

// #include "Workspace/Workspace.h"
#include "Commands/CommandIds.h"
#include "Platform/PlatformDialogs.h"
#include "Platform/X11/Runtime/X11Context.h"
#include "UI/Components/MenuModel.h"
#include "Utility/IcoDecoder.h"

#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

namespace Zenvra::Platform::X11 {

namespace {

constexpr long net_wm_state_remove = 0;
constexpr long net_wm_state_add = 1;
constexpr unsigned long double_click_interval_ms = 400;
constexpr int double_click_distance = 4;

unsigned long to_argb(const UI::Theme::Color &color) {
  return static_cast<unsigned long>(color.alpha) << 24U |
         static_cast<unsigned long>(color.red) << 16U |
         static_cast<unsigned long>(color.green) << 8U |
         static_cast<unsigned long>(color.blue);
}

/// Bilinear resample of an RGBA image into target_width x target_height.
std::vector<unsigned char> downsample_rgba(const unsigned char *source,
                                           int source_width,
                                           int source_height,
                                           int target_width,
                                           int target_height) {
  std::vector<unsigned char> result(
      static_cast<std::size_t>(target_width * target_height) * 4);
  if (source == nullptr || source_width <= 0 || source_height <= 0 ||
      target_width <= 0 || target_height <= 0) {
    return result;
  }
  const float scale_x =
      static_cast<float>(source_width) / static_cast<float>(target_width);
  const float scale_y =
      static_cast<float>(source_height) / static_cast<float>(target_height);
  for (int y = 0; y < target_height; ++y) {
    const float source_y = (static_cast<float>(y) + 0.5F) * scale_y - 0.5F;
    const int y0 = std::max(static_cast<int>(std::floor(source_y)), 0);
    const int y1 = std::min(y0 + 1, source_height - 1);
    const float fy = source_y - static_cast<float>(y0);
    for (int x = 0; x < target_width; ++x) {
      const float source_x = (static_cast<float>(x) + 0.5F) * scale_x - 0.5F;
      const int x0 = std::max(static_cast<int>(std::floor(source_x)), 0);
      const int x1 = std::min(x0 + 1, source_width - 1);
      const float fx = source_x - static_cast<float>(x0);
      for (int channel = 0; channel < 4; ++channel) {
        const float top =
            static_cast<float>(source[(y0 * source_width + x0) * 4 + channel]) *
                (1.0F - fx) +
            static_cast<float>(source[(y0 * source_width + x1) * 4 + channel]) *
                fx;
        const float bottom =
            static_cast<float>(source[(y1 * source_width + x0) * 4 + channel]) *
                (1.0F - fx) +
            static_cast<float>(source[(y1 * source_width + x1) * 4 + channel]) *
                fx;
        result[static_cast<std::size_t>(y * target_width + x) * 4 + channel] =
            static_cast<unsigned char>(
                top * (1.0F - fy) + bottom * fy + 0.5F);
      }
    }
  }
  return result;
}

struct EventTarget {
  Window main_window;
  Window popup_window;
};

Bool event_matches_window(Display *display, XEvent *event,
                          XPointer target_data) {
  static_cast<void>(display);
  const auto *target = reinterpret_cast<const EventTarget *>(target_data);
  return event->type == MappingNotify || event->xany.window == target->main_window ||
         (target->popup_window != 0 && event->xany.window == target->popup_window)
             ? True
             : False;
}

} // namespace

X11Window::X11Window(const WindowSpecification &specification)
    : m_specification(specification),
      m_client_width(static_cast<int>(specification.width)),
      m_client_height(static_cast<int>(specification.height)) {
  m_capabilities.custom_chrome = true;
  m_capabilities.native_titlebar_hit_test = true;
  m_capabilities.native_resize = true;
  m_capabilities.native_snap = true;
  m_capabilities.per_monitor_dpi = false;
}

X11Window::~X11Window() {
  release_native_resources();
  if (m_context_acquired) {
    Runtime::X11Context::shutdown();
  }
}

bool X11Window::initialize() {
  if (!Runtime::X11Context::initialize()) {
    return false;
  }
  m_context_acquired = true;

  m_display = Runtime::X11Context::get_display();
  if (m_display == nullptr) {
    std::cerr << "Fatal error: the X11 display connection is unavailable.\n";
    return false;
  }

  m_screen = DefaultScreen(m_display);
  m_dpi_scale = calculate_dpi_scale();
  const Window root_window = RootWindow(m_display, m_screen);
  m_window_handle = XCreateSimpleWindow(
      m_display, root_window, 0, 0, static_cast<unsigned int>(m_client_width),
      static_cast<unsigned int>(m_client_height), 0,
      BlackPixel(m_display, m_screen), BlackPixel(m_display, m_screen));
  if (m_window_handle == 0) {
    return false;
  }

  initialize_atoms();
  initialize_cursors();
  if (!m_file_drop_target.initialize(m_display, m_window_handle)) {
    std::clog << "Warning: X11 file drag-and-drop could not be initialized.\n";
  }
  m_ewmh_move_resize_supported =
      is_root_atom_supported(m_atoms.net_wm_move_resize);
  m_ewmh_maximize_supported =
      is_root_atom_supported(m_atoms.net_wm_state_maximized_horizontal) &&
      is_root_atom_supported(m_atoms.net_wm_state_maximized_vertical);
  m_capabilities.native_snap = m_ewmh_move_resize_supported;

  XStoreName(m_display, m_window_handle, m_specification.title.c_str());
  XChangeProperty(
      m_display, m_window_handle, m_atoms.net_wm_name, m_atoms.utf8_string, 8,
      PropModeReplace,
      reinterpret_cast<const unsigned char *>(m_specification.title.data()),
      static_cast<int>(m_specification.title.size()));
  XSetIconName(m_display, m_window_handle, m_specification.title.c_str());

  const Atom window_type = m_atoms.net_wm_window_type_normal;
  XChangeProperty(m_display, m_window_handle, m_atoms.net_wm_window_type,
                  XA_ATOM, 32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&window_type), 1);

  const unsigned long process_id = static_cast<unsigned long>(getpid());
  XChangeProperty(m_display, m_window_handle, m_atoms.net_wm_pid, XA_CARDINAL,
                  32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(&process_id), 1);

  XClassHint class_hint{};
  class_hint.res_name = const_cast<char *>("zde");
  class_hint.res_class = const_cast<char *>("ZenvraDevelopmentStudio");
  XSetClassHint(m_display, m_window_handle, &class_hint);

  Atom protocols[]{m_atoms.wm_delete_window};
  XSetWMProtocols(m_display, m_window_handle, protocols, 1);
  XSelectInput(m_display, m_window_handle,
               ExposureMask | KeyPressMask | ButtonPressMask |
                   ButtonReleaseMask | PointerMotionMask | StructureNotifyMask |
                   FocusChangeMask | PropertyChangeMask | EnterWindowMask |
                   LeaveWindowMask);

  if (!m_chrome_renderer.initialize(m_display, m_screen, m_dpi_scale,
                                    m_theme)) {
    std::cerr
        << "Fatal error: the X11 chrome renderer could not be initialized.\n";
    release_native_resources();
    return false;
  }
  
  apply_window_icon();
  refresh_chrome_layout();
  apply_size_hints();
  set_custom_chrome_enabled(m_specification.custom_chrome_enabled);
  XFlush(m_display);
  return true;
}

void X11Window::show() {
  if (m_display != nullptr && m_window_handle != 0) {
    XMapRaised(m_display, m_window_handle);
    XFlush(m_display);
  }
}

void X11Window::poll_events() {
  if (m_display == nullptr) {
    return;
  }

  EventTarget target{m_window_handle, m_chrome_renderer.popup_window()};

  XEvent event{};
  while (XCheckIfEvent(m_display, &event, event_matches_window,
                       reinterpret_cast<XPointer>(&target)) != 0) {
    if (target.popup_window != 0 && event.xany.window == target.popup_window && event.type != MappingNotify) {
      m_chrome_renderer.handle_popup_event(event);
      if (const auto command = m_chrome_renderer.take_popup_command()) {
        m_interaction_state.open_menu_index.reset();
        m_interaction_state.overflow_menu_open = false;
        m_interaction_state.hovered_popup_item_index.reset();
        m_interaction_state.hovered_overflow_menu_index.reset();
        m_pressed_popup_item_index.reset();
        m_menu_pointer_tracking = false;
        render();
        if (!command->empty()) {
          const std::optional<bool> editor_result =
              m_chrome_renderer.handle_editor_command(*command);
          if (editor_result) {
            if (*editor_result) {
              render();
            }
          } else if (m_command_invoked_callback) {
            m_command_invoked_callback(*command);
          }
        }
      }
    } else {
      handle_event(event);
    }
  }
  if (m_custom_chrome_enabled && m_chrome_renderer.tick_animations()) {
    render();
  }
}

bool X11Window::should_close() const { return m_should_close; }

void X11Window::minimize() {
  if (m_display != nullptr && m_window_handle != 0) {
    XIconifyWindow(m_display, m_window_handle, m_screen);
    XFlush(m_display);
  }
}

void X11Window::maximize() {
  if (m_display == nullptr || m_window_handle == 0 || m_is_maximized) {
    return;
  }

  if (m_ewmh_maximize_supported) {
    send_maximized_state(net_wm_state_add);
    return;
  }

  XWindowAttributes attributes{};
  Window child_window = None;
  int root_x = 0;
  int root_y = 0;
  if (XGetWindowAttributes(m_display, m_window_handle, &attributes) != 0 &&
      XTranslateCoordinates(m_display, m_window_handle,
                            RootWindow(m_display, m_screen), 0, 0, &root_x,
                            &root_y, &child_window) != 0) {
    m_restore_x = root_x;
    m_restore_y = root_y;
    m_restore_width = attributes.width;
    m_restore_height = attributes.height;
    m_restore_bounds_valid = true;
  }
  const WorkArea work_area = get_work_area();
  XMoveResizeWindow(m_display, m_window_handle, work_area.x, work_area.y,
                    static_cast<unsigned int>(work_area.width),
                    static_cast<unsigned int>(work_area.height));
  m_is_maximized = true;
  render();
}

void X11Window::restore() {
  if (m_display == nullptr || m_window_handle == 0) {
    return;
  }

  if (m_is_minimized && m_display != nullptr) {
    XMapRaised(m_display, m_window_handle);
  }
  if (m_ewmh_maximize_supported) {
    send_maximized_state(net_wm_state_remove);
    return;
  }

  if (m_restore_bounds_valid) {
    XMoveResizeWindow(m_display, m_window_handle, m_restore_x, m_restore_y,
                      static_cast<unsigned int>(m_restore_width),
                      static_cast<unsigned int>(m_restore_height));
  }
  m_is_maximized = false;
  render();
}

void X11Window::request_close() { m_should_close = true; }

bool X11Window::is_maximized() const { return m_is_maximized; }

bool X11Window::is_minimized() const { return m_is_minimized; }

bool X11Window::is_focused() const { return m_is_focused; }

const WindowCapabilities &X11Window::get_capabilities() const noexcept {
  return m_capabilities;
}

void *X11Window::get_native_handle() const noexcept {
  return reinterpret_cast<void *>(static_cast<std::uintptr_t>(m_window_handle));
}

void X11Window::set_custom_chrome_enabled(bool enabled) {
  m_custom_chrome_enabled = enabled && m_capabilities.custom_chrome;
  if (!m_custom_chrome_enabled) {
    m_interaction_state = Components::ChromeInteractionState{};
    m_pressed_popup_item_index.reset();
    m_menu_pointer_tracking = false;
  }
  refresh_chrome_layout();
  apply_custom_chrome();
  render();
}

void X11Window::set_titlebar_hit_test_callback(
    TitlebarHitTestCallback callback) {
  m_titlebar_hit_test_callback = std::move(callback);
}

void X11Window::set_command_invoked_callback(CommandInvokedCallback callback) {
  m_command_invoked_callback = std::move(callback);
}

void X11Window::set_command_state_query_callback(
    CommandStateQueryCallback callback) {
  m_command_state_query_callback = std::move(callback);
}

bool X11Window::open_project_folder() {
  if (!Zenvra::Platform::folder_dialog_available()) {
    std::cerr << "No folder dialog backend available (install kdialog, "
                 "zenity or yad).\n";
    return false;
  }
  const std::optional<std::filesystem::path> selected =
      Zenvra::Platform::open_folder_dialog();
  if (!selected || selected->empty()) {
    return true;
  }
  if (!m_chrome_renderer.set_workspace_root(*selected)) {
    std::cerr << "Could not open workspace folder: " << selected->string()
              << '\n';
    return true;
  }

  std::error_code path_error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(*selected, path_error);
  const std::filesystem::path display_root =
      path_error ? *selected : canonical;
  const std::string folder_name = display_root.filename().empty()
                                      ? display_root.string()
                                      : display_root.filename().string();
  const std::string window_title =
      folder_name + " - " + m_specification.title;
  m_specification.title = window_title;
  XStoreName(m_display, m_window_handle, m_specification.title.c_str());
  XChangeProperty(
      m_display, m_window_handle, m_atoms.net_wm_name, m_atoms.utf8_string, 8,
      PropModeReplace,
      reinterpret_cast<const unsigned char *>(m_specification.title.data()),
      static_cast<int>(m_specification.title.size()));
  XSetIconName(m_display, m_window_handle, m_specification.title.c_str());
  render();
  return true;
}

void X11Window::initialize_atoms() {
  m_atoms.wm_protocols = XInternAtom(m_display, "WM_PROTOCOLS", False);
  m_atoms.wm_delete_window = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
  m_atoms.utf8_string = XInternAtom(m_display, "UTF8_STRING", False);
  m_atoms.net_wm_name = XInternAtom(m_display, "_NET_WM_NAME", False);
  m_atoms.net_wm_icon = XInternAtom(m_display, "_NET_WM_ICON", False);
  m_atoms.net_wm_pid = XInternAtom(m_display, "_NET_WM_PID", False);
  m_atoms.net_wm_window_type =
      XInternAtom(m_display, "_NET_WM_WINDOW_TYPE", False);
  m_atoms.net_wm_window_type_normal =
      XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
  m_atoms.net_supported = XInternAtom(m_display, "_NET_SUPPORTED", False);
  m_atoms.net_workarea = XInternAtom(m_display, "_NET_WORKAREA", False);
  m_atoms.net_current_desktop =
      XInternAtom(m_display, "_NET_CURRENT_DESKTOP", False);
  m_atoms.net_wm_state = XInternAtom(m_display, "_NET_WM_STATE", False);
  m_atoms.net_wm_state_maximized_horizontal =
      XInternAtom(m_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
  m_atoms.net_wm_state_maximized_vertical =
      XInternAtom(m_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
  m_atoms.net_wm_state_hidden =
      XInternAtom(m_display, "_NET_WM_STATE_HIDDEN", False);
  m_atoms.net_wm_move_resize =
      XInternAtom(m_display, "_NET_WM_MOVERESIZE", False);
  m_atoms.motif_wm_hints = XInternAtom(m_display, "_MOTIF_WM_HINTS", False);
}

void X11Window::initialize_cursors() {
  m_default_cursor = XCreateFontCursor(m_display, XC_left_ptr);
  m_pointer_cursor = XCreateFontCursor(m_display, XC_hand2);
  m_text_cursor = XCreateFontCursor(m_display, XC_xterm);
  m_split_resize_cursor = XCreateFontCursor(m_display, XC_sb_v_double_arrow);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeTopLeft)] =
      XCreateFontCursor(m_display, XC_top_left_corner);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeTop)] =
      XCreateFontCursor(m_display, XC_top_side);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeTopRight)] =
      XCreateFontCursor(m_display, XC_top_right_corner);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeRight)] =
      XCreateFontCursor(m_display, XC_right_side);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeBottomRight)] =
      XCreateFontCursor(m_display, XC_bottom_right_corner);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeBottom)] =
      XCreateFontCursor(m_display, XC_bottom_side);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeBottomLeft)] =
      XCreateFontCursor(m_display, XC_bottom_left_corner);
  m_move_resize_cursors[static_cast<std::size_t>(
      MoveResizeDirection::SizeLeft)] =
      XCreateFontCursor(m_display, XC_left_side);
  m_move_resize_cursors[static_cast<std::size_t>(MoveResizeDirection::Move)] =
      XCreateFontCursor(m_display, XC_fleur);
  m_active_cursor = m_default_cursor;
  XDefineCursor(m_display, m_window_handle, m_default_cursor);
}

void X11Window::release_native_resources() {
  if (m_display == nullptr) {
    return;
  }

  if (m_manual_move_resize_direction) {
    XUngrabPointer(m_display, CurrentTime);
    m_manual_move_resize_direction.reset();
  }
  if (m_chrome_renderer.is_terminal_resizing()) {
    XUngrabPointer(m_display, CurrentTime);
    static_cast<void>(m_chrome_renderer.handle_workspace_pointer_release());
  }
  m_file_drop_target.shutdown();
  m_chrome_renderer.shutdown();
  for (Cursor &cursor : m_move_resize_cursors) {
    if (cursor != None) {
      XFreeCursor(m_display, cursor);
      cursor = None;
    }
  }
  if (m_default_cursor != None) {
    XFreeCursor(m_display, m_default_cursor);
    m_default_cursor = None;
  }
  if (m_text_cursor != None) {
    XFreeCursor(m_display, m_text_cursor);
    m_text_cursor = None;
  }
  if (m_pointer_cursor != None) {
    XFreeCursor(m_display, m_pointer_cursor);
    m_pointer_cursor = None;
  }
  if (m_split_resize_cursor != None) {
    XFreeCursor(m_display, m_split_resize_cursor);
    m_split_resize_cursor = None;
  }
  if (m_window_handle != 0) {
    XDestroyWindow(m_display, m_window_handle);
    m_window_handle = 0;
  }
}

void X11Window::apply_window_icon() const {
  constexpr std::array<int, 4> icon_sizes{16, 32, 48, 64};

  std::vector<unsigned long> icon_data;

  // Use the compiled-in bundled logo asset
  std::optional<Utility::DecodedImage> decoded = Utility::decode_ico_memory(Assets_icons_zenvra_logo_build_ico, Assets_icons_zenvra_logo_build_ico_len);

  if (decoded.has_value() && !decoded->pixels.empty()) {
    std::size_t icon_data_size = 0;
    for (const int icon_size : icon_sizes) {
      icon_data_size += 2U + static_cast<std::size_t>(icon_size * icon_size);
    }
    icon_data.reserve(icon_data_size);

    for (const int icon_size : icon_sizes) {
      const std::vector<unsigned char> resampled = downsample_rgba(
          decoded->pixels.data(), decoded->width, decoded->height, icon_size,
          icon_size);
      icon_data.push_back(static_cast<unsigned long>(icon_size));
      icon_data.push_back(static_cast<unsigned long>(icon_size));
      for (std::size_t pixel = 0;
           pixel < static_cast<std::size_t>(icon_size * icon_size); ++pixel) {
        const unsigned char red = resampled[pixel * 4 + 0];
        const unsigned char green = resampled[pixel * 4 + 1];
        const unsigned char blue = resampled[pixel * 4 + 2];
        const unsigned char alpha = resampled[pixel * 4 + 3];
        icon_data.push_back(
            static_cast<unsigned long>(alpha) << 24U |
            static_cast<unsigned long>(red) << 16U |
            static_cast<unsigned long>(green) << 8U |
            static_cast<unsigned long>(blue));
      }
    }
  } else {
    const unsigned long background_color = to_argb(m_theme.accent);
    const unsigned long glyph_color = 0xFFFFFFFFUL;

    std::size_t icon_data_size = 0;
    for (const int icon_size : icon_sizes) {
      icon_data_size += 2U + static_cast<std::size_t>(icon_size * icon_size);
    }
    icon_data.reserve(icon_data_size);

    for (const int icon_size : icon_sizes) {
      const int icon_margin = std::max(icon_size * 3 / 32, 1);
      const int glyph_start = icon_size / 4;
      const int glyph_end = icon_size - glyph_start - 1;
      const int glyph_thickness = std::max(icon_size / 24, 1);
      icon_data.push_back(static_cast<unsigned long>(icon_size));
      icon_data.push_back(static_cast<unsigned long>(icon_size));
      for (int point_y = 0; point_y < icon_size; ++point_y) {
        for (int point_x = 0; point_x < icon_size; ++point_x) {
          const bool in_background =
              point_x >= icon_margin && point_x < icon_size - icon_margin &&
              point_y >= icon_margin && point_y < icon_size - icon_margin;
          const bool on_horizontal =
              point_x >= glyph_start && point_x <= glyph_end &&
              (std::abs(point_y - glyph_start) <= glyph_thickness ||
               std::abs(point_y - glyph_end) <= glyph_thickness);
          const bool on_diagonal =
              point_x >= glyph_start && point_x <= glyph_end &&
              point_y >= glyph_start && point_y <= glyph_end &&
              std::abs(point_x + point_y - glyph_start - glyph_end) <=
                  glyph_thickness;
          icon_data.push_back(on_horizontal || on_diagonal
                                  ? glyph_color
                                  : (in_background ? background_color : 0UL));
        }
      }
    }
  }

  XChangeProperty(m_display, m_window_handle, m_atoms.net_wm_icon, XA_CARDINAL,
                  32, PropModeReplace,
                  reinterpret_cast<const unsigned char *>(icon_data.data()),
                  static_cast<int>(icon_data.size()));
}

void X11Window::apply_custom_chrome() {
  if (m_display == nullptr || m_window_handle == 0 ||
      m_atoms.motif_wm_hints == None) {
    return;
  }

  if (m_custom_chrome_enabled) {
    struct MotifWindowManagerHints {
      unsigned long flags;
      unsigned long functions;
      unsigned long decorations;
      long input_mode;
      unsigned long status;
    };

    constexpr unsigned long motif_hints_decorations = 1UL << 1U;
    const MotifWindowManagerHints hints{
        .flags = motif_hints_decorations,
        .functions = 0,
        .decorations = 0,
        .input_mode = 0,
        .status = 0,
    };
    XChangeProperty(m_display, m_window_handle, m_atoms.motif_wm_hints,
                    m_atoms.motif_wm_hints, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&hints), 5);
  } else {
    XDeleteProperty(m_display, m_window_handle, m_atoms.motif_wm_hints);
  }
  XFlush(m_display);
}

void X11Window::apply_size_hints() const {
  XSizeHints size_hints{};
  size_hints.flags = PMinSize;
  size_hints.min_width = static_cast<int>(720.0F * m_dpi_scale);
  size_hints.min_height = static_cast<int>(480.0F * m_dpi_scale);
  XSetWMNormalHints(m_display, m_window_handle, &size_hints);
}

void X11Window::refresh_chrome_layout() {
  m_chrome_layout = m_chrome_layout_engine.calculate(
      static_cast<float>(m_client_width), m_dpi_scale,
      UI::Chrome::WindowChromeLayoutOptions{
          .show_window_controls = m_custom_chrome_enabled,
          .hamburger_only = true,
      });
}

void X11Window::refresh_window_state() {
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long item_count = 0;
  unsigned long bytes_after = 0;
  unsigned char *property_data = nullptr;
  const int status = XGetWindowProperty(
      m_display, m_window_handle, m_atoms.net_wm_state, 0, 64, False, XA_ATOM,
      &actual_type, &actual_format, &item_count, &bytes_after, &property_data);
  if (status != Success) {
    if (property_data != nullptr) {
      XFree(property_data);
    }
    return;
  }

  if (actual_type == None) {
    m_is_maximized = false;
    m_is_minimized = false;
    m_interaction_state.maximized = false;
    return;
  }
  if (actual_type != XA_ATOM || actual_format != 32) {
    if (property_data != nullptr) {
      XFree(property_data);
    }
    return;
  }

  bool maximized_horizontal = false;
  bool maximized_vertical = false;
  bool hidden = false;
  if (property_data != nullptr) {
    const auto *states = reinterpret_cast<const Atom *>(property_data);
    for (unsigned long index = 0; index < item_count; ++index) {
      maximized_horizontal =
          maximized_horizontal ||
          states[index] == m_atoms.net_wm_state_maximized_horizontal;
      maximized_vertical =
          maximized_vertical ||
          states[index] == m_atoms.net_wm_state_maximized_vertical;
      hidden = hidden || states[index] == m_atoms.net_wm_state_hidden;
    }
    XFree(property_data);
  }
  m_is_maximized = maximized_horizontal && maximized_vertical;
  m_is_minimized = hidden;
  m_interaction_state.maximized = m_is_maximized;
}

void X11Window::render(std::optional<UI::Rect> dirty_rect) {
  if (m_display == nullptr || m_window_handle == 0) {
    return;
  }

  if (!m_custom_chrome_enabled) {
    XClearWindow(m_display, m_window_handle);
    return;
  }

  m_interaction_state.maximized = m_is_maximized;
  m_interaction_state.focused = m_is_focused;
  m_chrome_renderer.render(m_window_handle, m_client_width, m_client_height,
                           m_chrome_layout, m_interaction_state,
                           m_command_state_query_callback,
                           dirty_rect);
}

void X11Window::handle_event(XEvent &event) {
  switch (event.type) {
  case Expose: {
    const XExposeEvent &e = event.xexpose;
    if (e.count == 0) {
      const UI::Rect dirty_rect{
          static_cast<float>(e.x),
          static_cast<float>(e.y),
          static_cast<float>(e.width),
          static_cast<float>(e.height),
      };
      render(dirty_rect);
    }
    break;
  }

  case ConfigureNotify:
    if (event.xconfigure.width != m_client_width ||
        event.xconfigure.height != m_client_height) {
      m_client_width = event.xconfigure.width;
      m_client_height = event.xconfigure.height;
      refresh_chrome_layout();
      render();
    }
    break;

  case ClientMessage:
    if (m_file_drop_target.handle_client_message(event.xclient)) {
      break;
    }
    if (event.xclient.message_type == m_atoms.wm_protocols &&
        static_cast<Atom>(event.xclient.data.l[0]) ==
            m_atoms.wm_delete_window) {
      m_should_close = true;
    }
    break;

  case SelectionNotify:
    if (const std::optional<std::vector<std::filesystem::path>> dropped_paths =
            m_file_drop_target.handle_selection_notify(event.xselection)) {
      if (m_chrome_renderer.open_dropped_paths(*dropped_paths) > 0) {
        render();
      }
    }
    break;

  case DestroyNotify:
    m_window_handle = 0;
    m_should_close = true;
    break;

  case PropertyNotify:
    if (event.xproperty.atom == m_atoms.net_wm_state) {
      refresh_window_state();
      render();
    }
    break;

  case MapNotify:
    m_is_minimized = false;
    refresh_window_state();
    render();
    break;

  case UnmapNotify:
    m_is_minimized = true;
    break;

  case FocusIn:
    m_is_focused = true;
    render();
    break;

  case FocusOut:
    m_is_focused = false;
    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.hovered_overflow_menu_index.reset();
    m_interaction_state.pressed_control = UI::Chrome::WindowControl::NoControl;
    m_pressed_popup_item_index.reset();
    m_menu_pointer_tracking = false;
    render();
    break;

  case MotionNotify:
    if (m_custom_chrome_enabled) {
      handle_motion(event.xmotion);
    }
    break;

  case ButtonPress:
    if (m_custom_chrome_enabled) {
      handle_button_press(event.xbutton);
    }
    break;

  case ButtonRelease:
    if (m_custom_chrome_enabled) {
      handle_button_release(event.xbutton);
    }
    break;

  case LeaveNotify:
    m_interaction_state.hovered_control = UI::Chrome::WindowControl::NoControl;
    m_interaction_state.hovered_menu_index.reset();
    m_interaction_state.overflow_menu_hovered = false;
    m_interaction_state.hovered_overflow_menu_index.reset();
    m_interaction_state.command_center_hovered = false;
    m_interaction_state.run_button_hovered = false;
    m_interaction_state.debug_button_hovered = false;
    m_interaction_state.ellipsis_button_hovered = false;
    m_interaction_state.compiler_button_hovered = false;
    m_interaction_state.platform_button_hovered = false;
    m_interaction_state.binary_button_hovered = false;
    m_interaction_state.build_button_hovered = false;
    m_interaction_state.gear_button_hovered = false;
    static_cast<void>(m_chrome_renderer.handle_workspace_pointer_move(
        -10000.0F, -10000.0F, m_client_width, m_client_height,
        m_chrome_layout.titlebar_bounds.bottom()));
    render();
    break;

  case KeyPress:
    handle_key_press(event.xkey);
    break;

  case MappingNotify:
    XRefreshKeyboardMapping(&event.xmapping);
    break;

  default:
    break;
  }
}

void X11Window::handle_motion(const XMotionEvent &event) {
  if (m_manual_move_resize_direction) {
    update_manual_move_resize(event);
    return;
  }

  if ((event.state & Button1Mask) != 0 &&
      m_chrome_renderer.handle_workspace_pointer_drag(
          static_cast<float>(event.x), static_cast<float>(event.y),
          m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom())) {
    render();
    return;
  }

  const UI::Chrome::WindowControl hovered_control =
      m_chrome_layout.get_window_control(static_cast<float>(event.x),
                                         static_cast<float>(event.y));
  const std::optional<std::size_t> hovered_menu =
      m_chrome_layout.get_menu_index(static_cast<float>(event.x),
                                     static_cast<float>(event.y));
  const std::optional<std::size_t> hovered_popup_item =
      get_popup_item_index(event.x, event.y);
  const std::optional<std::size_t> hovered_overflow_menu =
      get_overflow_popup_menu_index(event.x, event.y);
  const bool overflow_menu_hovered = m_chrome_layout.is_overflow_menu(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool command_center_hovered =
      m_chrome_layout.command_center_bounds.contains(
          static_cast<float>(event.x), static_cast<float>(event.y));
  const bool run_button_hovered = m_chrome_layout.is_run_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool debug_button_hovered = m_chrome_layout.is_debug_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool ellipsis_button_hovered = m_chrome_layout.is_ellipsis_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool compiler_button_hovered = m_chrome_layout.is_compiler_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool platform_button_hovered = m_chrome_layout.is_platform_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool binary_button_hovered = m_chrome_layout.is_binary_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool build_button_hovered = m_chrome_layout.is_build_button(
      static_cast<float>(event.x), static_cast<float>(event.y));
  const bool gear_button_hovered = m_chrome_layout.is_gear_button(
      static_cast<float>(event.x), static_cast<float>(event.y));

  bool changed =
      hovered_control != m_interaction_state.hovered_control ||
      hovered_menu != m_interaction_state.hovered_menu_index ||
      hovered_popup_item != m_interaction_state.hovered_popup_item_index ||
      hovered_overflow_menu != m_interaction_state.hovered_overflow_menu_index ||
      overflow_menu_hovered != m_interaction_state.overflow_menu_hovered ||
      command_center_hovered != m_interaction_state.command_center_hovered ||
      run_button_hovered != m_interaction_state.run_button_hovered ||
      debug_button_hovered != m_interaction_state.debug_button_hovered ||
      ellipsis_button_hovered != m_interaction_state.ellipsis_button_hovered ||
      compiler_button_hovered != m_interaction_state.compiler_button_hovered ||
      platform_button_hovered != m_interaction_state.platform_button_hovered ||
      binary_button_hovered != m_interaction_state.binary_button_hovered ||
      build_button_hovered != m_interaction_state.build_button_hovered ||
      gear_button_hovered != m_interaction_state.gear_button_hovered;
  bool workspace_changed = false;
  if (m_interaction_state.open_menu_index.has_value() || m_interaction_state.overflow_menu_open || m_menu_pointer_tracking) {
      workspace_changed = m_chrome_renderer.handle_workspace_pointer_move(
                  -10000.0F, -10000.0F, m_client_width, m_client_height,
                  m_chrome_layout.titlebar_bounds.bottom());
  } else {
      workspace_changed = m_chrome_renderer.handle_workspace_pointer_move(
                  static_cast<float>(event.x), static_cast<float>(event.y),
                  m_client_width, m_client_height,
                  m_chrome_layout.titlebar_bounds.bottom());
  }
  changed = workspace_changed || changed;
  m_interaction_state.hovered_control = hovered_control;
  m_interaction_state.hovered_menu_index = hovered_menu;
  m_interaction_state.hovered_popup_item_index = hovered_popup_item;
  m_interaction_state.hovered_overflow_menu_index = hovered_overflow_menu;
  m_interaction_state.overflow_menu_hovered = overflow_menu_hovered;
  m_interaction_state.command_center_hovered = command_center_hovered;
  m_interaction_state.run_button_hovered = run_button_hovered;
  m_interaction_state.debug_button_hovered = debug_button_hovered;
  m_interaction_state.ellipsis_button_hovered = ellipsis_button_hovered;
  m_interaction_state.compiler_button_hovered = compiler_button_hovered;
  m_interaction_state.platform_button_hovered = platform_button_hovered;
  m_interaction_state.binary_button_hovered = binary_button_hovered;
  m_interaction_state.build_button_hovered = build_button_hovered;
  m_interaction_state.gear_button_hovered = gear_button_hovered;

  std::optional<std::size_t> combined_hovered_menu = hovered_menu;
  if (!combined_hovered_menu) {
    if (compiler_button_hovered) combined_hovered_menu = 10;
    else if (platform_button_hovered) combined_hovered_menu = 11;
    else if (binary_button_hovered) combined_hovered_menu = 12;
    else if (gear_button_hovered) combined_hovered_menu = 13;
    else if (ellipsis_button_hovered) combined_hovered_menu = 14;
  }

  // Hover-switching is now active for all dropdowns (menubar and overlays).
  const bool menu_interaction_active =
      m_interaction_state.open_menu_index.has_value() ||
      m_interaction_state.overflow_menu_open || m_menu_pointer_tracking;

  if (menu_interaction_active && hovered_overflow_menu &&
      m_interaction_state.overflow_menu_open &&
      m_interaction_state.open_menu_index != hovered_overflow_menu) {
    open_menu(*hovered_overflow_menu, false);
    m_interaction_state.hovered_popup_item_index.reset();
    m_pressed_popup_item_index.reset();
    changed = true;
  } else if (menu_interaction_active && combined_hovered_menu &&
             (m_interaction_state.open_menu_index != combined_hovered_menu ||
              m_interaction_state.overflow_menu_open)) {
    m_interaction_state.hovered_overflow_menu_index.reset();
    open_menu(*combined_hovered_menu, false);
    m_interaction_state.hovered_popup_item_index.reset();
    m_pressed_popup_item_index.reset();
    changed = true;
  } else if (menu_interaction_active && overflow_menu_hovered &&
             !m_interaction_state.overflow_menu_open) {
    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.overflow_menu_open = true;
    m_interaction_state.hovered_popup_item_index.reset();
    m_pressed_popup_item_index.reset();
    changed = true;
  }
  update_cursor(event.x, event.y);

  if (changed) {
    render();
  }
}

void X11Window::handle_button_press(const XButtonEvent &event) {
  if (event.button >= Button4 && event.button <= 7) {
    const float point_x = static_cast<float>(event.x);
    const float point_y = static_cast<float>(event.y);
    const float content_top = m_chrome_layout.titlebar_bounds.bottom();
    const bool over_editor =
        m_chrome_renderer.is_editor_point(point_x, point_y, m_client_width,
                                          m_client_height, content_top) ||
        m_chrome_renderer.is_minimap_point(point_x, point_y, m_client_width,
                                           m_client_height, content_top) ||
        m_chrome_renderer.is_scrollbar_point(point_x, point_y, m_client_width,
                                             m_client_height, content_top);
    const bool over_terminal = m_chrome_renderer.is_terminal_point(
        point_x, point_y, m_client_width, m_client_height, content_top);
    const bool over_tool_sidebar = m_chrome_renderer.is_tool_sidebar_point(
        point_x, point_y, m_client_width, m_client_height, content_top);
    const bool over_tab_bar = m_chrome_renderer.is_tab_bar_point(
        point_x, point_y, m_client_width, m_client_height, content_top);

    bool horizontal = (event.state & ShiftMask) != 0 || event.button == 6 || event.button == 7;
    int delta = (event.button == Button4 || event.button == 6) ? -3 : 3;

    if (over_tool_sidebar &&
        m_chrome_renderer.handle_tool_sidebar_scroll(
            delta, m_client_width, m_client_height,
            content_top)) {
      render();
      return;
    }
    if (over_terminal && m_chrome_renderer.handle_terminal_scroll(
                             delta, horizontal)) {
      render();
      return;
    }
    std::string command_out;
    if ((over_editor || over_tab_bar) &&
        m_chrome_renderer.handle_workspace_scroll(
            point_x, point_y, command_out, delta, horizontal, m_client_width,
            m_client_height, content_top)) {
      render();
      if (!command_out.empty() && m_command_invoked_callback) {
        m_command_invoked_callback(command_out);
      }
    }
    return;
  }
  if (event.button != Button1 && event.button != Button2) {
    return;
  }

  const float point_x = static_cast<float>(event.x);
  const float point_y = static_cast<float>(event.y);
  const bool tab_point = m_chrome_renderer.is_tab_bar_point(
      point_x, point_y, m_client_width, m_client_height,
      m_chrome_layout.titlebar_bounds.bottom());
  // Give the tab strip priority over the top resize frame.
  if (!tab_point) {
    const std::optional<MoveResizeDirection> resize_direction =
        get_resize_direction(event.x, event.y);
    if (resize_direction) {
      begin_move_resize(event, *resize_direction);
      return;
    }
  }

  const std::optional<std::size_t> menu_index =
      m_chrome_layout.get_menu_index(point_x, point_y);
  if (menu_index) {
    // Close any overlay dropdown before opening a menubar entry
    if (m_interaction_state.open_menu_index &&
        *m_interaction_state.open_menu_index >= UI::Chrome::window_menu_count) {
      m_interaction_state.open_menu_index.reset();
      m_interaction_state.hovered_popup_item_index.reset();
    }
    m_menu_pointer_tracking = true;
    open_menu(*menu_index, false);
    return;
  }

  if (m_chrome_layout.is_overflow_menu(point_x, point_y)) {
    if (m_interaction_state.overflow_menu_open) {
      m_chrome_renderer.close_popup();
      m_interaction_state.open_menu_index.reset();
      m_interaction_state.hovered_popup_item_index.reset();
      m_interaction_state.hovered_overflow_menu_index.reset();
      m_interaction_state.overflow_menu_open = false;
      m_menu_pointer_tracking = false;
      m_pressed_popup_item_index.reset();
    } else {
      m_chrome_renderer.close_popup();
      m_interaction_state.open_menu_index.reset();
      m_interaction_state.hovered_popup_item_index.reset();
      m_interaction_state.hovered_overflow_menu_index.reset();
      m_menu_pointer_tracking = true;
      m_interaction_state.overflow_menu_open = true;
    }
    render();
    return;
  }

  if (m_chrome_layout.is_run_button(point_x, point_y)) {
    // Close any open menu when clicking action buttons
    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.overflow_menu_open = false;
    const std::optional<bool> editor_result =
        m_chrome_renderer.handle_editor_command(
            Commands::CommandIds::run_start);
    if (!editor_result && m_command_invoked_callback) {
      m_command_invoked_callback(Commands::CommandIds::run_start);
    }
    return;
  }

  if (m_chrome_layout.is_debug_button(point_x, point_y)) {
    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.overflow_menu_open = false;
    const std::optional<bool> editor_result =
        m_chrome_renderer.handle_editor_command(
            Commands::CommandIds::view_problems);
    if (!editor_result && m_command_invoked_callback) {
      m_command_invoked_callback(Commands::CommandIds::view_problems);
    }
    return;
  }

  // Overlay toolbar dropdowns — mutual exclusion + toggle
  static constexpr std::size_t compiler_menu_index = 10;
  static constexpr std::size_t platform_menu_index = 11;
  static constexpr std::size_t binary_menu_index   = 12;
  static constexpr std::size_t gear_menu_index     = 13;
  static constexpr std::size_t ellipsis_menu_index = 14;

  auto open_or_close_overlay = [&](std::size_t idx) {
    // Toggle: if the same menu is already open, close it
    if (m_interaction_state.open_menu_index == idx) {
      m_chrome_renderer.close_popup();
      m_interaction_state.open_menu_index.reset();
      m_interaction_state.hovered_popup_item_index.reset();
      m_interaction_state.overflow_menu_open = false;
      m_pressed_popup_item_index.reset();
      render();
    } else {
      // Close whatever is open first, then open the new one
      m_interaction_state.hovered_overflow_menu_index.reset();
      open_menu(idx, false);
    }
  };

  if (m_chrome_layout.is_compiler_button(point_x, point_y)) {
    open_or_close_overlay(compiler_menu_index);
    return;
  }

  if (m_chrome_layout.is_platform_button(point_x, point_y)) {
    open_or_close_overlay(platform_menu_index);
    return;
  }

  if (m_chrome_layout.is_binary_button(point_x, point_y)) {
    open_or_close_overlay(binary_menu_index);
    return;
  }

  if (m_chrome_layout.is_gear_button(point_x, point_y)) {
    open_or_close_overlay(gear_menu_index);
    return;
  }

  if (m_chrome_layout.is_ellipsis_button(point_x, point_y)) {
    open_or_close_overlay(ellipsis_menu_index);
    return;
  }

  if (m_interaction_state.open_menu_index) {
    const std::optional<std::size_t> popup_item_index =
        get_popup_item_index(event.x, event.y);
    if (popup_item_index &&
        is_popup_item_enabled(*m_interaction_state.open_menu_index,
                              *popup_item_index)) {
      m_menu_pointer_tracking = true;
      m_pressed_popup_item_index = popup_item_index;
      m_interaction_state.hovered_popup_item_index = popup_item_index;
      render();
      return;
    }

    if (m_interaction_state.overflow_menu_open) {
      const std::optional<std::size_t> overflow_menu_index =
          get_overflow_popup_menu_index(event.x, event.y);
      if (overflow_menu_index) {
        m_menu_pointer_tracking = true;
        m_interaction_state.hovered_overflow_menu_index = overflow_menu_index;
        if (m_interaction_state.open_menu_index != overflow_menu_index) {
          m_interaction_state.open_menu_index = overflow_menu_index;
          m_interaction_state.hovered_popup_item_index.reset();
        }
        render();
        return;
      }
    }

    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_overflow_menu_index.reset();
    render();
    return;
  } else if (m_interaction_state.overflow_menu_open) {
    const std::optional<std::size_t> overflow_menu_index =
        get_overflow_popup_menu_index(event.x, event.y);
    if (overflow_menu_index) {
      m_menu_pointer_tracking = true;
      m_interaction_state.hovered_overflow_menu_index = overflow_menu_index;
      open_menu(*overflow_menu_index, false);
      render();
      return;
    }

    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_overflow_menu_index.reset();
    render();
    return;
  }

  const UI::Chrome::WindowControl control = m_chrome_layout.get_window_control(
      static_cast<float>(event.x), static_cast<float>(event.y));
  if (control != UI::Chrome::WindowControl::NoControl) {
    m_interaction_state.pressed_control = control;
    render();
    return;
  }

  const bool is_repeat_click =
      m_last_workspace_click_time != 0 &&
      event.time - m_last_workspace_click_time <= double_click_interval_ms &&
      std::abs(event.x - m_last_workspace_click_x) <= double_click_distance &&
      std::abs(event.y - m_last_workspace_click_y) <= double_click_distance;
  const int click_count =
      is_repeat_click ? std::min(m_workspace_click_count + 1, 3) : 1;
  m_last_workspace_click_time = event.time;
  m_last_workspace_click_x = event.x;
  m_last_workspace_click_y = event.y;
  m_workspace_click_count = click_count;

  std::string command_out;
  if (m_chrome_renderer.handle_workspace_pointer_press(
          point_x, point_y, m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom(),
          (event.state & ShiftMask) != 0, click_count, event.time,
          command_out)) {
    if (!command_out.empty() && m_command_invoked_callback) {
      m_command_invoked_callback(command_out);
    }
    if (m_chrome_renderer.is_terminal_resizing()) {
      XGrabPointer(m_display, m_window_handle, False,
                   ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                   GrabModeAsync, None, m_split_resize_cursor, event.time);
    }
    render();
    return;
  }

  if (!is_drag_region(point_x, point_y)) {
    return;
  }

  const bool is_double_click =
      m_last_titlebar_click_time != 0 &&
      event.time - m_last_titlebar_click_time <= double_click_interval_ms &&
      std::abs(event.x - m_last_titlebar_click_x) <= double_click_distance &&
      std::abs(event.y - m_last_titlebar_click_y) <= double_click_distance;
  if (is_double_click) {
    m_last_titlebar_click_time = 0;
    is_maximized() ? restore() : maximize();
    return;
  }

  m_last_titlebar_click_time = event.time;
  m_last_titlebar_click_x = event.x;
  m_last_titlebar_click_y = event.y;
  begin_move_resize(event, MoveResizeDirection::Move);
}

void X11Window::handle_button_release(const XButtonEvent &event) {
  if (event.button != Button1) {
    return;
  }

  if (m_manual_move_resize_direction) {
    end_manual_move_resize(event.time);
    return;
  }

  const bool terminal_was_resizing = m_chrome_renderer.is_terminal_resizing();
  if (m_chrome_renderer.handle_workspace_pointer_release()) {
    if (terminal_was_resizing) {
      XUngrabPointer(m_display, event.time);
    }
    render();
    return;
  }

  if (m_menu_pointer_tracking) {
    m_menu_pointer_tracking = false;
    if (m_interaction_state.open_menu_index) {
      const std::optional<std::size_t> popup_item_index =
          get_popup_item_index(event.x, event.y);
      if (popup_item_index &&
          is_popup_item_enabled(*m_interaction_state.open_menu_index,
                                *popup_item_index)) {
        m_interaction_state.hovered_popup_item_index = popup_item_index;
        execute_popup_selection();
        return;
      }
    }

    if (m_interaction_state.overflow_menu_open) {
      const std::optional<std::size_t> overflow_menu_index =
          get_overflow_popup_menu_index(event.x, event.y);
      if (overflow_menu_index) {
        open_menu(*overflow_menu_index, false);
        return;
      }
    }

    const std::optional<std::size_t> menu_index =
        m_chrome_layout.get_menu_index(static_cast<float>(event.x),
                                         static_cast<float>(event.y));
    if (menu_index) {
      open_menu(*menu_index, false);
      return;
    }
    if (m_chrome_layout.is_overflow_menu(static_cast<float>(event.x),
                                         static_cast<float>(event.y))) {
      m_interaction_state.open_menu_index.reset();
      m_interaction_state.overflow_menu_open = true;
      m_interaction_state.hovered_popup_item_index.reset();
      render();
      return;
    }

    m_pressed_popup_item_index.reset();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.hovered_overflow_menu_index.reset();
    render();
    return;
  }

  const UI::Chrome::WindowControl released_control =
      m_chrome_layout.get_window_control(static_cast<float>(event.x),
                                         static_cast<float>(event.y));
  const UI::Chrome::WindowControl pressed_control =
      m_interaction_state.pressed_control;
  m_interaction_state.pressed_control = UI::Chrome::WindowControl::NoControl;
  if (pressed_control == released_control) {
    switch (pressed_control) {
    case UI::Chrome::WindowControl::Minimize:
      minimize();
      break;
    case UI::Chrome::WindowControl::MaximizeRestore:
      is_maximized() ? restore() : maximize();
      break;
    case UI::Chrome::WindowControl::Close:
      request_close();
      break;
    case UI::Chrome::WindowControl::NoControl:
      break;
    }
  }
  render();
}

void X11Window::handle_key_press(XKeyEvent &event) {
  const KeySym key_symbol = XLookupKeysym(&event, 0);
  if ((event.state & Mod1Mask) != 0 && key_symbol == XK_F4) {
    request_close();
    return;
  }
  if (!m_custom_chrome_enabled) {
    return;
  }

  std::optional<std::size_t> accelerator_menu_index;
  if ((event.state & Mod1Mask) != 0) {
    switch (key_symbol) {
    case XK_f:
    case XK_F:
      accelerator_menu_index = 0;
      break;
    case XK_e:
    case XK_E:
      accelerator_menu_index = 1;
      break;
    case XK_s:
    case XK_S:
      accelerator_menu_index = 2;
      break;
    case XK_v:
    case XK_V:
      accelerator_menu_index = 3;
      break;
    case XK_n:
    case XK_N:
      accelerator_menu_index = 4;
      break;
    case XK_p:
    case XK_P:
      accelerator_menu_index = 5;
      break;
    case XK_b:
    case XK_B:
      accelerator_menu_index = 6;
      break;
    case XK_r:
    case XK_R:
      accelerator_menu_index = 7;
      break;
    case XK_w:
    case XK_W:
      accelerator_menu_index = 8;
      break;
    case XK_h:
    case XK_H:
      accelerator_menu_index = 9;
      break;
    default:
      break;
    }
  }
  if (accelerator_menu_index &&
      *accelerator_menu_index < UI::Chrome::window_menu_count) {
    open_menu(*accelerator_menu_index, true);
    return;
  }

  if (key_symbol == XK_Escape && (m_interaction_state.open_menu_index ||
                                  m_interaction_state.overflow_menu_open)) {
    m_chrome_renderer.close_popup();
    m_interaction_state.open_menu_index.reset();
    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_popup_item_index.reset();
    m_interaction_state.hovered_overflow_menu_index.reset();
    m_pressed_popup_item_index.reset();
    m_menu_pointer_tracking = false;
    render();
    return;
  }

  if (!m_interaction_state.open_menu_index &&
      !m_interaction_state.overflow_menu_open &&
      m_chrome_renderer.is_terminal_focused() &&
      (event.state & Mod1Mask) == 0) {
    bool handled = false;
    if ((event.state & ControlMask) != 0 &&
        ((key_symbol >= XK_a && key_symbol <= XK_z) ||
         (key_symbol >= XK_A && key_symbol <= XK_Z))) {
      handled = m_chrome_renderer.handle_terminal_control(static_cast<char>(
          key_symbol >= XK_A && key_symbol <= XK_Z ? key_symbol - XK_A + 'A'
                                                   : key_symbol - XK_a + 'a'));
    } else {
      std::optional<Terminal::TerminalInputKey> terminal_key;
      switch (key_symbol) {
      case XK_Return:
      case XK_KP_Enter:
        terminal_key = Terminal::TerminalInputKey::Enter;
        break;
      case XK_BackSpace:
        terminal_key = Terminal::TerminalInputKey::Backspace;
        break;
      case XK_Tab:
      case XK_KP_Tab:
        terminal_key = Terminal::TerminalInputKey::Tab;
        break;
      case XK_Escape:
        terminal_key = Terminal::TerminalInputKey::Escape;
        break;
      case XK_Up:
      case XK_KP_Up:
        terminal_key = Terminal::TerminalInputKey::ArrowUp;
        break;
      case XK_Down:
      case XK_KP_Down:
        terminal_key = Terminal::TerminalInputKey::ArrowDown;
        break;
      case XK_Left:
      case XK_KP_Left:
        terminal_key = Terminal::TerminalInputKey::ArrowLeft;
        break;
      case XK_Right:
      case XK_KP_Right:
        terminal_key = Terminal::TerminalInputKey::ArrowRight;
        break;
      case XK_Home:
      case XK_KP_Home:
        terminal_key = Terminal::TerminalInputKey::Home;
        break;
      case XK_End:
      case XK_KP_End:
        terminal_key = Terminal::TerminalInputKey::End;
        break;
      case XK_Delete:
      case XK_KP_Delete:
        terminal_key = Terminal::TerminalInputKey::DeleteForward;
        break;
      default:
        break;
      }

      if (terminal_key) {
        handled = m_chrome_renderer.handle_terminal_key(*terminal_key);
      } else if ((event.state & ControlMask) == 0) {
        char text[64]{};
        KeySym input_symbol = NoSymbol;
        const int text_length =
            XLookupString(&event, text, static_cast<int>(sizeof(text)),
                          &input_symbol, nullptr);
        handled = text_length > 0 &&
                  m_chrome_renderer.handle_text_input(std::string_view{
                      text, static_cast<std::size_t>(text_length)});
      }
    }

    if (handled) {
      render();
    }
    return;
  }

  if (!m_interaction_state.open_menu_index &&
      !m_interaction_state.overflow_menu_open &&
      m_chrome_renderer.is_editor_focused() &&
      (event.state & ControlMask) != 0) {
    std::optional<UI::Editor::EditorAction> action;
    if ((event.state & ShiftMask) != 0 &&
        (key_symbol == XK_Delete || key_symbol == XK_KP_Delete)) {
      action = UI::Editor::EditorAction::RemoveDocument;
    } else {
      switch (key_symbol) {
      case XK_n:
      case XK_N:
        action = UI::Editor::EditorAction::CreateDocument;
        break;
      case XK_s:
      case XK_S:
        action = UI::Editor::EditorAction::SaveDocument;
        break;
      case XK_w:
      case XK_W:
        action = UI::Editor::EditorAction::CloseDocument;
        break;
      case XK_a:
      case XK_A:
        action = UI::Editor::EditorAction::SelectAll;
        break;
      case XK_c:
      case XK_C:
        action = UI::Editor::EditorAction::Copy;
        break;
      case XK_x:
      case XK_X:
        action = UI::Editor::EditorAction::Cut;
        break;
      case XK_v:
      case XK_V:
        action = UI::Editor::EditorAction::Paste;
        break;
      case XK_slash:
        action = UI::Editor::EditorAction::ToggleComment;
        break;
      default:
        break;
      }
    }
    if (action) {
      if (m_chrome_renderer.handle_editor_action(*action)) {
        render();
      }
      return;
    }
  }

  if (!m_interaction_state.open_menu_index &&
      !m_interaction_state.overflow_menu_open &&
      m_chrome_renderer.is_editor_focused()) {
    const bool alt_pressed = (event.state & Mod1Mask) != 0;
    const bool ctrl_pressed = (event.state & ControlMask) != 0;
    const bool shift_pressed = (event.state & ShiftMask) != 0;

    if (key_symbol == XK_Escape) {
      if (m_chrome_renderer.handle_editor_input(UI::Editor::EditorInputCommand::Escape, false)) {
        render();
        return;
      }
    }

    if ((ctrl_pressed && shift_pressed && (key_symbol == XK_Up || key_symbol == XK_KP_Up || key_symbol == XK_Down || key_symbol == XK_KP_Down)) ||
        (ctrl_pressed && alt_pressed && (key_symbol == XK_Up || key_symbol == XK_KP_Up || key_symbol == XK_Down || key_symbol == XK_KP_Down))) {
      const auto cmd = (key_symbol == XK_Up || key_symbol == XK_KP_Up)
          ? UI::Editor::EditorInputCommand::AddCursorAbove
          : UI::Editor::EditorInputCommand::AddCursorBelow;
      if (m_chrome_renderer.handle_editor_input(cmd, false)) {
        render();
      }
      return;
    }

    if (alt_pressed && !ctrl_pressed) {
      std::optional<UI::Editor::EditorInputCommand> editor_command;
      if (key_symbol == XK_Up || key_symbol == XK_KP_Up) {
        editor_command = UI::Editor::EditorInputCommand::MoveLineUp;
      } else if (key_symbol == XK_Down || key_symbol == XK_KP_Down) {
        editor_command = UI::Editor::EditorInputCommand::MoveLineDown;
      }
      if (editor_command) {
        if (m_chrome_renderer.handle_editor_input(*editor_command, false)) {
          render();
        }
        return;
      }
    }
  }

  if (!m_interaction_state.open_menu_index &&
      !m_interaction_state.overflow_menu_open &&
      m_chrome_renderer.is_editor_focused() && (event.state & Mod1Mask) == 0) {
    std::optional<UI::Editor::EditorInputCommand> editor_command;
    switch (key_symbol) {
    case XK_Left:
    case XK_KP_Left:
      editor_command = UI::Editor::EditorInputCommand::MoveLeft;
      break;
    case XK_Right:
    case XK_KP_Right:
      editor_command = UI::Editor::EditorInputCommand::MoveRight;
      break;
    case XK_Up:
    case XK_KP_Up:
      editor_command = UI::Editor::EditorInputCommand::MoveUp;
      break;
    case XK_Down:
    case XK_KP_Down:
      editor_command = UI::Editor::EditorInputCommand::MoveDown;
      break;
    case XK_Home:
    case XK_KP_Home:
      editor_command = UI::Editor::EditorInputCommand::MoveHome;
      break;
    case XK_End:
    case XK_KP_End:
      editor_command = UI::Editor::EditorInputCommand::MoveEnd;
      break;
    case XK_Return:
    case XK_KP_Enter:
      editor_command = UI::Editor::EditorInputCommand::InsertNewLine;
      break;
    case XK_Tab:
    case XK_KP_Tab:
      editor_command = UI::Editor::EditorInputCommand::InsertTab;
      break;
    case XK_BackSpace:
      editor_command = UI::Editor::EditorInputCommand::DeleteBackward;
      break;
    case XK_Delete:
    case XK_KP_Delete:
      editor_command = UI::Editor::EditorInputCommand::DeleteForward;
      break;
    default:
      break;
    }
    if (editor_command) {
      if (m_chrome_renderer.handle_editor_input(
              *editor_command, (event.state & ShiftMask) != 0)) {
        render();
      }
      return;
    }
    if ((event.state & ControlMask) == 0) {
      char text[64]{};
      KeySym input_symbol = NoSymbol;
      const int text_length = XLookupString(
          &event, text, static_cast<int>(sizeof(text)), &input_symbol, nullptr);
      if (text_length > 0 && static_cast<unsigned char>(text[0]) >= 0x20U &&
          static_cast<unsigned char>(text[0]) != 0x7FU &&
          m_chrome_renderer.handle_text_input(
              std::string_view{text, static_cast<std::size_t>(text_length)})) {
        render();
      }
      return;
    }
  }

  if (!m_interaction_state.open_menu_index) {
    return;
  }

  if (key_symbol == XK_Left || key_symbol == XK_Right) {
    constexpr std::size_t menu_count = UI::Chrome::window_menu_count;
    if (menu_count > 0) {
      const std::size_t current_index = *m_interaction_state.open_menu_index;
      const std::size_t next_index =
          key_symbol == XK_Right
              ? (current_index + 1) % menu_count
              : (current_index + menu_count - 1) % menu_count;
      open_menu(next_index, true);
    }
    return;
  }
  if (key_symbol == XK_Down || key_symbol == XK_Up) {
    move_popup_selection(key_symbol == XK_Down ? 1 : -1);
    return;
  }
  if (key_symbol == XK_Return || key_symbol == XK_KP_Enter ||
      key_symbol == XK_space) {
    execute_popup_selection();
  }
}

void X11Window::update_cursor(int point_x, int point_y) {
  Cursor desired_cursor = m_default_cursor;
  if (m_interaction_state.open_menu_index ||
      m_interaction_state.overflow_menu_open) {
    if (desired_cursor != None && desired_cursor != m_active_cursor) {
      XDefineCursor(m_display, m_window_handle, desired_cursor);
      m_active_cursor = desired_cursor;
    }
    return;
  }
  const bool tab_point = m_chrome_renderer.is_tab_bar_point(
      static_cast<float>(point_x), static_cast<float>(point_y), m_client_width,
      m_client_height, m_chrome_layout.titlebar_bounds.bottom());
  const bool is_sidebar =
      m_chrome_renderer.is_activity_bar_point(
          static_cast<float>(point_x), static_cast<float>(point_y),
          m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom()) ||
      m_chrome_renderer.is_tool_sidebar_point(
          static_cast<float>(point_x), static_cast<float>(point_y),
          m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom());
  const bool is_run_or_debug =
      m_chrome_layout.is_run_button(static_cast<float>(point_x),
                                    static_cast<float>(point_y)) ||
      m_chrome_layout.is_debug_button(static_cast<float>(point_x),
                                      static_cast<float>(point_y));
  const bool is_toolbar_button =
      m_chrome_layout.is_ellipsis_button(static_cast<float>(point_x),
                                         static_cast<float>(point_y)) ||
      m_chrome_layout.is_compiler_button(static_cast<float>(point_x),
                                         static_cast<float>(point_y)) ||
      m_chrome_layout.is_platform_button(static_cast<float>(point_x),
                                         static_cast<float>(point_y)) ||
      m_chrome_layout.is_binary_button(static_cast<float>(point_x),
                                       static_cast<float>(point_y)) ||
      m_chrome_layout.is_build_button(static_cast<float>(point_x),
                                      static_cast<float>(point_y)) ||
      m_chrome_layout.is_gear_button(static_cast<float>(point_x),
                                     static_cast<float>(point_y));
  const bool is_empty_state_button = m_chrome_renderer.is_empty_state_button_hovered();
  const bool is_fold_marker =
      m_chrome_renderer.is_fold_margin_point(
          static_cast<float>(point_x), static_cast<float>(point_y),
          m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom());
  const std::optional<MoveResizeDirection> direction =
      (tab_point || is_run_or_debug || is_toolbar_button || is_sidebar ||
       is_empty_state_button)
          ? std::nullopt
          : get_resize_direction(point_x, point_y);
  if (tab_point || is_run_or_debug || is_toolbar_button || is_sidebar ||
      is_empty_state_button || is_fold_marker) {
    desired_cursor = m_pointer_cursor;
  } else if (direction) {
    desired_cursor =
        m_move_resize_cursors[static_cast<std::size_t>(*direction)];
  } else if (is_drag_region(static_cast<float>(point_x),
                            static_cast<float>(point_y))) {
    desired_cursor = m_move_resize_cursors[static_cast<std::size_t>(
        MoveResizeDirection::Move)];
  } else if (m_chrome_renderer.is_terminal_resize_handle_point(
                 static_cast<float>(point_x), static_cast<float>(point_y),
                 m_client_width, m_client_height,
                 m_chrome_layout.titlebar_bounds.bottom())) {
    desired_cursor = m_split_resize_cursor;
  } else if (m_chrome_renderer.is_editor_point(
                 static_cast<float>(point_x), static_cast<float>(point_y),
                 m_client_width, m_client_height,
                 m_chrome_layout.titlebar_bounds.bottom()) ||
             m_chrome_renderer.is_terminal_point(
                 static_cast<float>(point_x), static_cast<float>(point_y),
                 m_client_width, m_client_height,
                 m_chrome_layout.titlebar_bounds.bottom())) {
    desired_cursor = m_text_cursor;
  }

  if (desired_cursor != None && desired_cursor != m_active_cursor) {
    XDefineCursor(m_display, m_window_handle, desired_cursor);
    m_active_cursor = desired_cursor;
  }
}

void X11Window::begin_move_resize(const XButtonEvent &event,
                                  MoveResizeDirection direction) {
  if (!m_ewmh_move_resize_supported) {
    XWindowAttributes attributes{};
    Window child_window = None;
    if (XGetWindowAttributes(m_display, m_window_handle, &attributes) == 0 ||
        XTranslateCoordinates(m_display, m_window_handle,
                              RootWindow(m_display, m_screen), 0, 0,
                              &m_manual_start_window_x,
                              &m_manual_start_window_y, &child_window) == 0) {
      return;
    }

    m_manual_start_root_x = event.x_root;
    m_manual_start_root_y = event.y_root;
    m_manual_start_width = attributes.width;
    m_manual_start_height = attributes.height;
    const int grab_result = XGrabPointer(
        m_display, m_window_handle, False,
        ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
        None, m_move_resize_cursors[static_cast<std::size_t>(direction)],
        event.time);
    if (grab_result == GrabSuccess) {
      m_manual_move_resize_direction = direction;
    }
    return;
  }

  XUngrabPointer(m_display, event.time);

  XEvent message{};
  message.xclient.type = ClientMessage;
  message.xclient.display = m_display;
  message.xclient.window = m_window_handle;
  message.xclient.message_type = m_atoms.net_wm_move_resize;
  message.xclient.format = 32;
  message.xclient.data.l[0] = event.x_root;
  message.xclient.data.l[1] = event.y_root;
  message.xclient.data.l[2] = static_cast<long>(direction);
  message.xclient.data.l[3] = Button1;
  message.xclient.data.l[4] = 1;
  XSendEvent(m_display, RootWindow(m_display, m_screen), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &message);
  XFlush(m_display);
}

void X11Window::update_manual_move_resize(const XMotionEvent &event) {
  if (!m_manual_move_resize_direction) {
    return;
  }

  const int delta_x = event.x_root - m_manual_start_root_x;
  const int delta_y = event.y_root - m_manual_start_root_y;
  int window_x = m_manual_start_window_x;
  int window_y = m_manual_start_window_y;
  int window_width = m_manual_start_width;
  int window_height = m_manual_start_height;
  const int minimum_width = static_cast<int>(720.0F * m_dpi_scale);
  const int minimum_height = static_cast<int>(480.0F * m_dpi_scale);

  const MoveResizeDirection direction = *m_manual_move_resize_direction;
  if (direction == MoveResizeDirection::Move) {
    window_x += delta_x;
    window_y += delta_y;
  } else {
    const bool resize_left = direction == MoveResizeDirection::SizeLeft ||
                             direction == MoveResizeDirection::SizeTopLeft ||
                             direction == MoveResizeDirection::SizeBottomLeft;
    const bool resize_right = direction == MoveResizeDirection::SizeRight ||
                              direction == MoveResizeDirection::SizeTopRight ||
                              direction == MoveResizeDirection::SizeBottomRight;
    const bool resize_top = direction == MoveResizeDirection::SizeTop ||
                            direction == MoveResizeDirection::SizeTopLeft ||
                            direction == MoveResizeDirection::SizeTopRight;
    const bool resize_bottom =
        direction == MoveResizeDirection::SizeBottom ||
        direction == MoveResizeDirection::SizeBottomLeft ||
        direction == MoveResizeDirection::SizeBottomRight;

    if (resize_left) {
      const int applied_delta =
          std::min(delta_x, m_manual_start_width - minimum_width);
      window_x += applied_delta;
      window_width -= applied_delta;
    }
    if (resize_right) {
      window_width = std::max(m_manual_start_width + delta_x, minimum_width);
    }
    if (resize_top) {
      const int applied_delta =
          std::min(delta_y, m_manual_start_height - minimum_height);
      window_y += applied_delta;
      window_height -= applied_delta;
    }
    if (resize_bottom) {
      window_height = std::max(m_manual_start_height + delta_y, minimum_height);
    }
  }

  XMoveResizeWindow(m_display, m_window_handle, window_x, window_y,
                    static_cast<unsigned int>(window_width),
                    static_cast<unsigned int>(window_height));
  XFlush(m_display);
}

void X11Window::end_manual_move_resize(Time event_time) {
  m_manual_move_resize_direction.reset();
  XUngrabPointer(m_display, event_time);
  if (m_default_cursor != None && m_default_cursor != m_active_cursor) {
    XDefineCursor(m_display, m_window_handle, m_default_cursor);
    m_active_cursor = m_default_cursor;
  }
}

void X11Window::send_maximized_state(long operation) {
  if (m_display == nullptr || m_window_handle == 0) {
    return;
  }

  XEvent event{};
  event.xclient.type = ClientMessage;
  event.xclient.display = m_display;
  event.xclient.window = m_window_handle;
  event.xclient.message_type = m_atoms.net_wm_state;
  event.xclient.format = 32;
  event.xclient.data.l[0] = operation;
  event.xclient.data.l[1] =
      static_cast<long>(m_atoms.net_wm_state_maximized_horizontal);
  event.xclient.data.l[2] =
      static_cast<long>(m_atoms.net_wm_state_maximized_vertical);
  event.xclient.data.l[3] = 1;
  XSendEvent(m_display, RootWindow(m_display, m_screen), False,
             SubstructureRedirectMask | SubstructureNotifyMask, &event);
  m_is_maximized = operation == net_wm_state_add;
  m_interaction_state.maximized = m_is_maximized;
  render();
  XFlush(m_display);
}

void X11Window::open_menu(std::size_t menu_index, bool select_first_item, const UI::Rect* anchor_override) {
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size()) {
    return;
  }

  const UI::Rect *anchor_bounds = anchor_override;
  bool side_popup = false;
  UI::Rect overflow_anchor;

  if (anchor_bounds == nullptr) {
    for (std::size_t index = 0; index < m_chrome_layout.visible_menu_count; ++index) {
      if (m_chrome_layout.menu_regions[index].menu_index == menu_index) {
        anchor_bounds = &m_chrome_layout.menu_regions[index].bounds;
        break;
      }
    }
    if (anchor_bounds == nullptr && menu_index < UI::Chrome::window_menu_count &&
        m_chrome_layout.has_overflow_menu()) {
      auto overflow_geom = m_chrome_renderer.calculate_overflow_menu_geometry(m_chrome_layout);
      if (menu_index >= overflow_geom.first_menu_index && menu_index < overflow_geom.first_menu_index + overflow_geom.item_count) {
        std::size_t item_index = menu_index - overflow_geom.first_menu_index;
        overflow_anchor = overflow_geom.item_bounds[item_index];
        anchor_bounds = &overflow_anchor;
        side_popup = true;
      } else {
        anchor_bounds = &m_chrome_layout.overflow_menu_bounds;
      }
    }

    if (anchor_bounds == nullptr) {
      static constexpr std::size_t compiler_menu_index = 10;
      static constexpr std::size_t platform_menu_index = 11;
      static constexpr std::size_t binary_menu_index   = 12;
      static constexpr std::size_t gear_menu_index     = 13;
      static constexpr std::size_t ellipsis_menu_index = 14;

      if (menu_index == compiler_menu_index)
        anchor_bounds = &m_chrome_layout.compiler_bounds;
      else if (menu_index == platform_menu_index)
        anchor_bounds = &m_chrome_layout.platform_bounds;
      else if (menu_index == binary_menu_index)
        anchor_bounds = &m_chrome_layout.binary_bounds;
      else if (menu_index == gear_menu_index)
        anchor_bounds = &m_chrome_layout.gear_bounds;
      else if (menu_index == ellipsis_menu_index)
        anchor_bounds = &m_chrome_layout.ellipsis_bounds;
    }
  }

  if (anchor_bounds == nullptr) return;

  if (!side_popup) {
    m_interaction_state.overflow_menu_open = false;
    m_interaction_state.hovered_overflow_menu_index.reset();
  }

  std::vector<Components::PopupMenuItem> popup_items;
  for (const auto &item : menus[menu_index].items) {
    Components::PopupMenuItem popup_item;
    popup_item.text = std::string(item.label);
    popup_item.separator = item.separator;
    popup_item.command_id = std::string(item.command_id);
    popup_item.shortcut = std::string(item.shortcut);
    if (!item.command_id.empty()) {
      const auto state = (!m_command_state_query_callback ||
                          m_command_state_query_callback(item.command_id).enabled);
      popup_item.enabled = state;
    }
    popup_items.push_back(std::move(popup_item));
  }

  if (!m_chrome_renderer.open_popup(m_window_handle, *anchor_bounds, popup_items,
                                    select_first_item, side_popup)) {
    return;
  }

  m_interaction_state.open_menu_index = menu_index;
  m_interaction_state.hovered_popup_item_index = select_first_item ? 0 : std::optional<std::size_t>();
  m_pressed_popup_item_index.reset();
  render();
}

void X11Window::move_popup_selection(int direction) {
  if (!m_interaction_state.open_menu_index || direction == 0) {
    return;
  }

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  const std::size_t menu_index = *m_interaction_state.open_menu_index;
  if (menu_index >= menus.size() || menus[menu_index].items.empty()) {
    return;
  }

  const std::size_t item_count = menus[menu_index].items.size();
  std::ptrdiff_t candidate =
      m_interaction_state.hovered_popup_item_index
          ? static_cast<std::ptrdiff_t>(
                *m_interaction_state.hovered_popup_item_index)
          : (direction > 0 ? -1 : static_cast<std::ptrdiff_t>(item_count));
  for (std::size_t attempt = 0; attempt < item_count; ++attempt) {
    candidate += direction > 0 ? 1 : -1;
    if (candidate < 0) {
      candidate = static_cast<std::ptrdiff_t>(item_count) - 1;
    } else if (candidate >= static_cast<std::ptrdiff_t>(item_count)) {
      candidate = 0;
    }

    if (is_popup_item_enabled(menu_index,
                              static_cast<std::size_t>(candidate))) {
      m_interaction_state.hovered_popup_item_index =
          static_cast<std::size_t>(candidate);
      render();
      return;
    }
  }

  m_interaction_state.hovered_popup_item_index.reset();
  render();
}

void X11Window::execute_popup_selection() {
  if (!m_interaction_state.open_menu_index ||
      !m_interaction_state.hovered_popup_item_index) {
    return;
  }

  const std::size_t menu_index = *m_interaction_state.open_menu_index;
  const std::size_t item_index = *m_interaction_state.hovered_popup_item_index;
  if (!is_popup_item_enabled(menu_index, item_index)) {
    return;
  }

  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  const std::string_view command_id =
      menus[menu_index].items[item_index].command_id;
  m_interaction_state.open_menu_index.reset();
  m_interaction_state.overflow_menu_open = false;
  m_interaction_state.hovered_popup_item_index.reset();
  m_interaction_state.hovered_overflow_menu_index.reset();
  m_pressed_popup_item_index.reset();
  render();
  if (command_id.empty()) {
    return;
  }
  const std::optional<bool> editor_result =
      m_chrome_renderer.handle_editor_command(command_id);
  if (editor_result) {
    if (*editor_result) {
      render();
    }
    return;
  }
  if (m_command_invoked_callback) {
    m_command_invoked_callback(command_id);
  }
}

float X11Window::calculate_dpi_scale() const {
  const char *resource_manager = XResourceManagerString(m_display);
  if (resource_manager != nullptr) {
    const char *dpi_entry = std::strstr(resource_manager, "Xft.dpi:");
    if (dpi_entry != nullptr) {
      const char *dpi_value = std::strchr(dpi_entry, ':');
      if (dpi_value != nullptr) {
        const float dpi = std::strtof(dpi_value + 1, nullptr);
        if (dpi >= 48.0F && dpi <= 768.0F) {
          return std::clamp(dpi / 96.0F, 0.5F, 4.0F);
        }
      }
    }
  }

  const int width_pixels = DisplayWidth(m_display, m_screen);
  const int width_millimeters = DisplayWidthMM(m_display, m_screen);
  if (width_pixels > 0 && width_millimeters > 0) {
    const float dpi = static_cast<float>(width_pixels) * 25.4F /
                      static_cast<float>(width_millimeters);
    return std::clamp(dpi / 96.0F, 0.75F, 3.0F);
  }
  return 1.0F;
}

X11Window::WorkArea X11Window::get_work_area() const {
  WorkArea work_area{
      .x = 0,
      .y = 0,
      .width = DisplayWidth(m_display, m_screen),
      .height = DisplayHeight(m_display, m_screen),
  };

  unsigned long current_desktop = 0;
  Atom actual_type = None;
  int actual_format = 0;
  unsigned long item_count = 0;
  unsigned long bytes_after = 0;
  unsigned char *property_data = nullptr;
  int status = XGetWindowProperty(m_display, RootWindow(m_display, m_screen),
                                  m_atoms.net_current_desktop, 0, 1, False,
                                  XA_CARDINAL, &actual_type, &actual_format,
                                  &item_count, &bytes_after, &property_data);
  if (status == Success && actual_type == XA_CARDINAL && actual_format == 32 &&
      item_count == 1 && property_data != nullptr) {
    current_desktop = *reinterpret_cast<const unsigned long *>(property_data);
  }
  if (property_data != nullptr) {
    XFree(property_data);
    property_data = nullptr;
  }
  if (current_desktop > 1024) {
    current_desktop = 0;
  }

  status = XGetWindowProperty(
      m_display, RootWindow(m_display, m_screen), m_atoms.net_workarea,
      static_cast<long>(current_desktop * 4), 4, False, XA_CARDINAL,
      &actual_type, &actual_format, &item_count, &bytes_after, &property_data);
  if (status == Success && actual_type == XA_CARDINAL && actual_format == 32 &&
      item_count == 4 && property_data != nullptr) {
    const auto *values = reinterpret_cast<const unsigned long *>(property_data);
    if (values[2] > 0 && values[3] > 0) {
      work_area.x = static_cast<int>(values[0]);
      work_area.y = static_cast<int>(values[1]);
      work_area.width = static_cast<int>(values[2]);
      work_area.height = static_cast<int>(values[3]);
    }
  }
  if (property_data != nullptr) {
    XFree(property_data);
  }
  return work_area;
}

bool X11Window::is_drag_region(float point_x, float point_y) const {
  if (m_chrome_renderer.is_tab_bar_point(
          point_x, point_y, m_client_width, m_client_height,
          m_chrome_layout.titlebar_bounds.bottom())) {
    return false;
  }
  return m_titlebar_hit_test_callback
             ? m_titlebar_hit_test_callback(point_x, point_y)
             : m_chrome_layout.is_drag_region(point_x, point_y);
}

std::optional<X11Window::MoveResizeDirection>
X11Window::get_resize_direction(int point_x, int point_y) const {
  if (!m_custom_chrome_enabled || m_is_maximized || m_client_width <= 0 ||
      m_client_height <= 0) {
    return std::nullopt;
  }

  const int resize_border =
      std::max(static_cast<int>(std::lround(6.0F * m_dpi_scale)), 4);
  const bool on_left = point_x >= 0 && point_x < resize_border;
  const bool on_right =
      point_x < m_client_width && point_x >= m_client_width - resize_border;
  const bool on_top = point_y >= 0 && point_y < resize_border;
  const bool on_bottom =
      point_y < m_client_height && point_y >= m_client_height - resize_border;

  if (on_top && on_left) {
    return MoveResizeDirection::SizeTopLeft;
  }
  if (on_top && on_right) {
    return MoveResizeDirection::SizeTopRight;
  }
  if (on_bottom && on_right) {
    return MoveResizeDirection::SizeBottomRight;
  }
  if (on_bottom && on_left) {
    return MoveResizeDirection::SizeBottomLeft;
  }
  if (on_top) {
    return MoveResizeDirection::SizeTop;
  }
  if (on_right) {
    return MoveResizeDirection::SizeRight;
  }
  if (on_bottom) {
    return MoveResizeDirection::SizeBottom;
  }
  if (on_left) {
    return MoveResizeDirection::SizeLeft;
  }
  return std::nullopt;
}

std::optional<std::size_t> X11Window::get_popup_item_index(int point_x,
                                                           int point_y) const {
  if (!m_interaction_state.open_menu_index) {
    return std::nullopt;
  }

  const Components::PopupMenuGeometry geometry =
      m_chrome_renderer.calculate_popup_geometry(
          m_chrome_layout, *m_interaction_state.open_menu_index,
          m_interaction_state.overflow_menu_open);
  for (std::size_t item_index = 0; item_index < geometry.item_count;
       ++item_index) {
    if (geometry.item_bounds[item_index].contains(
            static_cast<float>(point_x), static_cast<float>(point_y))) {
      const std::span<const UI::Components::Menu> menus =
          UI::Components::get_window_menus();
      if (*m_interaction_state.open_menu_index < menus.size() &&
          item_index <
              menus[*m_interaction_state.open_menu_index].items.size() &&
          !menus[*m_interaction_state.open_menu_index]
               .items[item_index]
               .separator) {
        return item_index;
      }
    }
  }
  
  // Debug log every 30th motion event if we have a menu open
  static int debug_counter = 0;
  if (++debug_counter % 30 == 0) {
    std::clog << "[DBG] get_popup_item_index miss! pos=(" << point_x << "," << point_y << ") "
              << "geom_bounds=(" << geometry.bounds.x << "," << geometry.bounds.y 
              << " " << geometry.bounds.width << "x" << geometry.bounds.height << ") "
              << "items=" << geometry.item_count << "\n";
  }

  return std::nullopt;
}

std::optional<std::size_t>
X11Window::get_overflow_popup_menu_index(int point_x, int point_y) const {
  if (!m_interaction_state.overflow_menu_open) {
    return std::nullopt;
  }

  const Components::OverflowMenuGeometry geometry =
      m_chrome_renderer.calculate_overflow_menu_geometry(m_chrome_layout);
  for (std::size_t item_index = 0; item_index < geometry.item_count;
       ++item_index) {
    if (geometry.item_bounds[item_index].contains(
            static_cast<float>(point_x), static_cast<float>(point_y))) {
      return geometry.first_menu_index + item_index;
    }
  }
  return std::nullopt;
}

bool X11Window::is_popup_item_enabled(std::size_t menu_index,
                                      std::size_t item_index) const {
  const std::span<const UI::Components::Menu> menus =
      UI::Components::get_window_menus();
  if (menu_index >= menus.size() ||
      item_index >= menus[menu_index].items.size()) {
    return false;
  }

  const UI::Components::MenuItem &item = menus[menu_index].items[item_index];
  if (item.separator || item.command_id.empty()) {
    return false;
  }
  const std::optional<bool> editor_enabled =
      m_chrome_renderer.is_editor_command_enabled(item.command_id);
  return editor_enabled
             ? *editor_enabled
             : (!m_command_state_query_callback ||
                m_command_state_query_callback(item.command_id).enabled);
}

bool X11Window::is_root_atom_supported(Atom atom) const {
  if (atom == None || m_atoms.net_supported == None) {
    return false;
  }

  Atom actual_type = None;
  int actual_format = 0;
  unsigned long item_count = 0;
  unsigned long bytes_after = 0;
  unsigned char *property_data = nullptr;
  const int status = XGetWindowProperty(
      m_display, RootWindow(m_display, m_screen), m_atoms.net_supported, 0,
      1024, False, XA_ATOM, &actual_type, &actual_format, &item_count,
      &bytes_after, &property_data);
  if (status != Success || property_data == nullptr || actual_type != XA_ATOM ||
      actual_format != 32) {
    if (property_data != nullptr) {
      XFree(property_data);
    }
    return false;
  }

  bool supported = false;
  const auto *supported_atoms = reinterpret_cast<const Atom *>(property_data);
  for (unsigned long index = 0; index < item_count; ++index) {
    if (supported_atoms[index] == atom) {
      supported = true;
      break;
    }
  }
  XFree(property_data);
  return supported;
}

} // namespace Zenvra::Platform::X11
