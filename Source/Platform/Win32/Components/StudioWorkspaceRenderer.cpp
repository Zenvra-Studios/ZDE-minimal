#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Commands/CommandIds.h"
#include "Utility/Antialiasing.h"
#include "Utility/stb_image.h"

#include "UI/Editor/EditorFileSystem.h"
#include "Utility/Fonts.h"
#include <lunasvg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "Msimg32.lib")

namespace Zenvra::Platform::Win32::Components {

namespace {

int round_to_int(float value) { return static_cast<int>(std::lround(value)); }

COLORREF to_color_ref(const UI::Theme::Color &color) {
  return RGB(color.red, color.green, color.blue);
}

std::string to_font_color(const UI::Theme::Color &color) {
  char value[8]{};
  std::snprintf(value, sizeof(value), "#%02x%02x%02x",
                static_cast<unsigned int>(color.red),
                static_cast<unsigned int>(color.green),
                static_cast<unsigned int>(color.blue));
  return value;
}

RECT to_native_rect(const UI::Rect &rectangle) {
  return RECT{
      round_to_int(rectangle.x),
      round_to_int(rectangle.y),
      round_to_int(rectangle.right()),
      round_to_int(rectangle.bottom()),
  };
}

} // namespace

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer() { shutdown(); }

bool StudioWorkspaceRenderer::initialize(UINT dpi) {
  shutdown();
  m_dpi = std::max(dpi, 48U);
  m_dpi_scale = static_cast<float>(m_dpi) / 96.0F;

  std::error_code path_error;
  const std::filesystem::path current_path =
      std::filesystem::current_path(path_error);
  std::optional<std::filesystem::path> project_root;
  if (!path_error) {
    project_root =
        UI::Editor::EditorFileSystem::find_project_root(current_path);
  }
  if (!project_root) {
    std::array<wchar_t, 32768> executable_path{};
    const DWORD length =
        GetModuleFileNameW(nullptr, executable_path.data(),
                           static_cast<DWORD>(executable_path.size()));
    if (length > 0 && length < executable_path.size()) {
      project_root = UI::Editor::EditorFileSystem::find_project_root(
          std::filesystem::path{executable_path.data()});
    }
  }
  if (project_root) {
    m_icon_asset_root = *project_root / "Assets" / "icons";
  }

  // Load bundled fonts from Assets/fonts/. For each font, register the TTF
  // file privately using AddFontResourceExA when found (and not a placeholder).
  // If not found or the file is invalid, fall back to the system default.
  bool hack_loaded = false;
  bool opensans_loaded = false;
  if (project_root) {
    const std::filesystem::path hack_ttf = *project_root / "Assets" / "fonts" /
                                           "Hack" / "ttf" / "Hack-Regular.ttf";
    const std::filesystem::path opensans_ttf = *project_root / "Assets" /
                                               "fonts" / "OpenSans" /
                                               "OpenSans-Regular.ttf";
    std::error_code size_error;

    // Hack – editor / minimap / terminal font
    if (std::filesystem::exists(hack_ttf, size_error) &&
        std::filesystem::file_size(hack_ttf, size_error) > 100) {
      hack_loaded = AddFontResourceExA(hack_ttf.string().c_str(), FR_PRIVATE,
                                       nullptr) > 0;
    }

    // JetBrains Mono – specs / code / monospace font
    const std::filesystem::path jb_ttf = *project_root / "Assets" / "fonts" /
                                         "JetBrainsMono" /
                                         "JetBrainsMonoNLNerdFont-Regular.ttf";
    const std::filesystem::path jb_bold_ttf =
        *project_root / "Assets" / "fonts" / "JetBrainsMono" /
        "JetBrainsMonoNLNerdFont-Bold.ttf";
    if (std::filesystem::exists(jb_ttf, size_error) &&
        std::filesystem::file_size(jb_ttf, size_error) > 100) {
      AddFontResourceExA(jb_ttf.string().c_str(), FR_PRIVATE, nullptr);
    }
    if (std::filesystem::exists(jb_bold_ttf, size_error) &&
        std::filesystem::file_size(jb_bold_ttf, size_error) > 100) {
      AddFontResourceExA(jb_bold_ttf.string().c_str(), FR_PRIVATE, nullptr);
    }

    // Open Sans – UI / sidebar / tab / large title font
    if (std::filesystem::exists(opensans_ttf, size_error) &&
        std::filesystem::file_size(opensans_ttf, size_error) > 100) {
      opensans_loaded = AddFontResourceExA(opensans_ttf.string().c_str(),
                                           FR_PRIVATE, nullptr) > 0;
    }
  }

  const char *editor_font_name = hack_loaded ? "Hack" : "Consolas";
  const char *ui_font_name = opensans_loaded ? "Open Sans" : "Segoe UI";

  m_ui_font = std::make_unique<AntialiasedFont>(
      ui_font_name, std::max(round_to_int(12.0F * m_dpi_scale), 9));
  m_small_font = std::make_unique<AntialiasedFont>(
      ui_font_name, std::max(round_to_int(12.0F * m_dpi_scale), 9));
  m_editor_font = std::make_unique<AntialiasedFont>(
      editor_font_name, std::max(round_to_int(14.0F * m_dpi_scale), 10));
  m_editor_font->setLigaturesEnabled(true);
  m_minimap_font = std::make_unique<AntialiasedFont>(
      editor_font_name, std::max(round_to_int(3.0F * m_dpi_scale), 3));
  m_large_font = std::make_unique<AntialiasedFont>(
      ui_font_name, std::max(round_to_int(24.0F * m_dpi_scale), 18), FW_BOLD);
  if (!m_ui_font->isValid() || !m_small_font->isValid() ||
      !m_editor_font->isValid() || !m_minimap_font->isValid() ||
      !m_large_font->isValid()) {
    shutdown();
    return false;
  }
  static_cast<void>(m_tool_sidebar.initialize());
  static_cast<void>(m_terminal_panel.toggle());
  m_terminal_panel.set_focused(false);
  static_cast<void>(m_shader_sandbox_panel.initialize());
  return true;
}

UI::Editor::StudioEditorLayoutResult
StudioWorkspaceRenderer::calculate_layout(int client_width, int client_height,
                                          float content_top) const noexcept {
  return m_layout_engine.calculate(
      static_cast<float>(client_width), static_cast<float>(client_height),
      content_top, m_dpi_scale, m_terminal_panel.is_visible(),
      m_terminal_panel.get_height(), m_terminal_panel.is_maximized(),
      m_tool_sidebar.is_visible(), m_tool_sidebar.get_width(),
      m_shader_sandbox_panel.is_visible(), m_shader_sandbox_panel.get_width());
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path &path) {
  const bool res = m_text_editor.open_file(path);
  if (res && m_shader_sandbox_panel.is_visible()) {
    if (const UI::Editor::TextDocumentModel *doc =
            m_text_editor.get_document()) {
      std::string full_text;
      for (const auto &line : doc->get_lines()) {
        full_text += line;
        full_text += '\n';
      }
      if (!full_text.empty()) {
        m_shader_sandbox_panel.set_source_code(full_text);
      }
    }
  }
  return res;
}

bool StudioWorkspaceRenderer::set_workspace_root(
    const std::filesystem::path &root) {
  if (!m_tool_sidebar.set_workspace_root(root)) {
    return false;
  }
  m_terminal_panel.set_working_directory(root);
  return true;
}

std::size_t StudioWorkspaceRenderer::open_dropped_paths(
    std::span<const std::filesystem::path> dropped_paths) {
  return m_text_editor.open_dropped_paths(dropped_paths);
}

bool StudioWorkspaceRenderer::create_buffer() {
  return m_text_editor.create_buffer();
}

bool StudioWorkspaceRenderer::handle_pointer_press(
    HDC device_context, float point_x, float point_y, int client_width,
    int client_height, float content_top, bool extend_selection,
    std::string &command_out) {
  if (m_prompt_modal.is_visible()) {
    const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                            static_cast<float>(client_height)};
    const auto prompt_layout =
        m_prompt_modal.calculate_layout(viewport, m_dpi_scale);
    return m_prompt_modal.handle_pointer_press(point_x, point_y, prompt_layout);
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (const std::optional<std::size_t> sidebar_index =
          UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y)) {
    const std::span<const UI::Editor::SidebarItem> items =
        UI::Editor::get_studio_sidebar_items();
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Terminal) {
      return m_terminal_panel.toggle();
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Shader) {
      const bool res = m_shader_sandbox_panel.toggle();
      if (res) {
        if (const UI::Editor::TextDocumentModel *doc =
                m_text_editor.get_document()) {
          std::string full_text;
          for (const auto &line : doc->get_lines()) {
            full_text += line;
            full_text += '\n';
          }
          if (!full_text.empty()) {
            m_shader_sandbox_panel.set_source_code(full_text);
          }
        }
      }
      return true;
    }
    return m_tool_sidebar.activate(items[*sidebar_index].icon);
  }
  if (m_shader_sandbox_panel.handle_pointer_press(layout, point_x, point_y)) {
    return true;
  }
  const SidebarPressResult sidebar_res =
      m_tool_sidebar.handle_pointer_press(layout, point_x, point_y);
  if (sidebar_res.handled) {
    m_terminal_panel.set_focused(false);
    if (sidebar_res.action == SidebarActionKind::OpenFile && sidebar_res.path) {
      static_cast<void>(m_text_editor.open_file(*sidebar_res.path));
    } else if (sidebar_res.action == SidebarActionKind::NewFile &&
               sidebar_res.path) {
      const auto root = m_tool_sidebar.get_model().get_workspace_root();
      const std::string proj_name = root.filename().string();
      m_add_item_dialog.open(
          m_window_handle,
          *sidebar_res.path, proj_name,
          [this](const std::string &name, const std::string &initial_content) {
            std::filesystem::path created_p;
            if (m_tool_sidebar.get_model().create_file(name, created_p)) {
              if (!initial_content.empty()) {
                std::ofstream out(created_p, std::ios::binary);
                if (out.is_open()) {
                  out.write(initial_content.data(), initial_content.size());
                  out.close();
                }
              }
              static_cast<void>(m_text_editor.open_file(created_p));
            }
          });
    } else if (sidebar_res.action == SidebarActionKind::NewFolder &&
               sidebar_res.path) {
      m_prompt_modal.open_new_folder(
          *sidebar_res.path, [this](const std::string &name) {
            std::filesystem::path created_p;
            m_tool_sidebar.get_model().create_directory(name, created_p);
          });
    }
    return true;
  }
  if (m_terminal_panel.handle_pointer_press(layout, point_x, point_y)) {
    return true;
  }
  m_terminal_panel.set_focused(false);
  return m_text_editor.handle_pointer_press(*this, device_context, layout,
                                            point_x, point_y, extend_selection,
                                            command_out);
}

bool StudioWorkspaceRenderer::handle_double_click(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.handle_double_click(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_move(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) noexcept {
  if (m_prompt_modal.is_visible()) {
    const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                            static_cast<float>(client_height)};
    const auto prompt_layout =
        m_prompt_modal.calculate_layout(viewport, m_dpi_scale);
    return m_prompt_modal.handle_pointer_move(point_x, point_y, prompt_layout);
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  const bool sidebar_changed =
      m_tool_sidebar.handle_pointer_move(layout, point_x, point_y);
  const bool editor_changed =
      m_text_editor.handle_pointer_move(layout, point_x, point_y);
  const bool shader_changed =
      m_shader_sandbox_panel.handle_pointer_move(layout, point_x, point_y);
  return m_terminal_panel.handle_pointer_move(layout, point_x, point_y) ||
         sidebar_changed || editor_changed || shader_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(HDC device_context,
                                                  float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_tool_sidebar.is_resizing()) {
    return m_tool_sidebar.handle_pointer_drag(layout, point_x);
  }
  if (m_shader_sandbox_panel.is_resizing() ||
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    if (m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  if (m_terminal_panel.is_resizing()) {
    return m_terminal_panel.handle_pointer_drag(layout, point_y);
  }
  if (m_terminal_panel.handle_pointer_drag(layout, point_x, point_y)) {
    return true;
  }
  return m_text_editor.handle_pointer_drag(*this, device_context, layout,
                                           point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept {
  const bool terminal_changed = m_terminal_panel.handle_pointer_release();
  const bool sidebar_changed = m_tool_sidebar.handle_pointer_release();
  const bool editor_changed = m_text_editor.handle_pointer_release();
  const bool shader_changed = m_shader_sandbox_panel.handle_pointer_release();
  return terminal_changed || sidebar_changed || editor_changed ||
         shader_changed;
}

bool StudioWorkspaceRenderer::handle_scroll(const Event::ScrollEvent &event,
                                            int client_width, int client_height,
                                            float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.handle_scroll(*this, layout, event);
}

bool StudioWorkspaceRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command, bool extend_selection) {
  return m_text_editor.handle_input(command, extend_selection);
}

bool StudioWorkspaceRenderer::handle_editor_action(
    UI::Editor::EditorAction action) {
  return m_text_editor.handle_action(action);
}

std::optional<bool>
StudioWorkspaceRenderer::handle_editor_command(std::string_view command_id) {
  if (command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel" ||
      command_id == "zde.view.shader_sandbox") {
    const bool res = m_shader_sandbox_panel.toggle();
    if (res) {
      if (const UI::Editor::TextDocumentModel *doc =
              m_text_editor.get_document()) {
        std::string full_text;
        for (const auto &line : doc->get_lines()) {
          full_text += line;
          full_text += '\n';
        }
        if (!full_text.empty()) {
          m_shader_sandbox_panel.set_source_code(full_text);
        }
      }
    }
    return res;
  }
  return m_text_editor.handle_command(command_id);
}

std::optional<bool> StudioWorkspaceRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept {
  if (command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel" ||
      command_id == "zde.view.shader_sandbox") {
    return true;
  }
  return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text) {
  const bool res = m_terminal_panel.is_focused()
                       ? m_terminal_panel.handle_text_input(utf8_text)
                       : m_text_editor.handle_text_input(utf8_text);
  if (res && !m_terminal_panel.is_focused() &&
      m_shader_sandbox_panel.is_visible()) {
    if (const UI::Editor::TextDocumentModel *doc =
            m_text_editor.get_document()) {
      std::string full_text;
      for (const auto &line : doc->get_lines()) {
        full_text += line;
        full_text += '\n';
      }
      if (!full_text.empty()) {
        m_shader_sandbox_panel.set_source_code(full_text);
      }
    }
  }
  return res;
}

bool StudioWorkspaceRenderer::handle_terminal_key(
    Terminal::TerminalInputKey key) {
  return m_terminal_panel.handle_key(key);
}

bool StudioWorkspaceRenderer::handle_terminal_control(char letter) {
  return m_terminal_panel.handle_control(letter);
}

bool StudioWorkspaceRenderer::handle_terminal_scroll(
    const Event::ScrollEvent &event) noexcept {
  return m_terminal_panel.handle_scroll(event);
}

bool StudioWorkspaceRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta, int client_width, int client_height,
    float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.handle_scroll(layout, line_delta);
}

bool StudioWorkspaceRenderer::is_editor_focused() const noexcept {
  return !m_terminal_panel.is_focused() && m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_terminal_focused() const noexcept {
  return m_terminal_panel.is_focused();
}

bool StudioWorkspaceRenderer::is_activity_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return layout.activity_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_ui_font == nullptr) {
    return false;
  }
  HDC device_context = GetDC(nullptr);
  if (device_context == nullptr) {
    return false;
  }
  const bool interactive = m_text_editor.is_tab_interactive_point(
      *this, device_context, layout, point_x, point_y);
  ReleaseDC(nullptr, device_context);
  return interactive;
}

bool StudioWorkspaceRenderer::is_editor_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return (layout.gutter_bounds.contains(point_x, point_y) ||
          layout.editor_bounds.contains(point_x, point_y)) &&
         !layout.minimap_bounds.contains(point_x, point_y) &&
         !layout.scrollbar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_interactive_point(
    float point_x, float point_y) const noexcept {
  return m_text_editor.is_empty_state_interactive_point(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_scrollbar_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_fold_margin_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_fold_margin_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_minimap_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_visible() &&
         layout.terminal_content_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_tool_sidebar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_interactive_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_resizing() const noexcept {
  return m_terminal_panel.is_resizing();
}

bool StudioWorkspaceRenderer::is_sidebar_resize_handle_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.is_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_sidebar_resizing() const noexcept {
  return m_tool_sidebar.is_resizing();
}

bool StudioWorkspaceRenderer::is_shader_sandbox_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_shader_sandbox_resize_handle(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.is_resize_handle_point(layout, point_x,
                                                       point_y);
}

bool StudioWorkspaceRenderer::is_shader_sandbox_resizing() const noexcept {
  return m_shader_sandbox_panel.is_resizing();
}

bool StudioWorkspaceRenderer::toggle_shader_sandbox() noexcept {
  const bool res = m_shader_sandbox_panel.toggle();
  if (res) {
    if (const UI::Editor::TextDocumentModel *doc =
            m_text_editor.get_document()) {
      std::string full_text;
      for (const auto &line : doc->get_lines()) {
        full_text += line;
        full_text += '\n';
      }
      if (!full_text.empty()) {
        m_shader_sandbox_panel.set_source_code(full_text);
      }
    }
  }
  return res;
}

bool StudioWorkspaceRenderer::is_shader_sandbox_visible() const noexcept {
  return m_shader_sandbox_panel.is_visible();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept {
  const bool caret_changed = m_text_editor.tick_animations();
  const bool terminal_changed = m_terminal_panel.poll();
  const bool terminal_anim_changed = m_terminal_panel.tick_animations();
  const bool shader_changed = m_shader_sandbox_panel.tick_animations();
  return caret_changed || terminal_changed || terminal_anim_changed ||
         shader_changed;
}

void StudioWorkspaceRenderer::shutdown() {
  m_terminal_panel.shutdown();
  m_svg_cache.clear();
  m_icon_asset_root.clear();
  m_minimap_font.reset();
  m_editor_font.reset();
  m_small_font.reset();
  m_ui_font.reset();
}

void StudioWorkspaceRenderer::render(HDC device_context, int client_width,
                                     int client_height,
                                     float content_top) const {
  if (device_context == nullptr || m_ui_font == nullptr ||
      m_small_font == nullptr || m_editor_font == nullptr ||
      m_minimap_font == nullptr) {
    return;
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  fill_rectangle(device_context, layout.workspace_bounds,
                 m_palette.workspace_background);
  fill_rectangle(device_context, layout.tab_bar_bounds,
                 m_palette.tab_background);
  fill_rectangle(device_context, layout.activity_bar_bounds,
                 m_palette.sidebar_background);
  fill_rectangle(device_context, layout.tool_sidebar_bounds,
                 m_palette.sidebar_background);
  fill_rectangle(device_context, layout.gutter_bounds,
                 m_palette.editor_background);
  fill_rectangle(device_context, layout.editor_bounds,
                 m_palette.editor_background);
  fill_rectangle(device_context, layout.status_bar_bounds,
                 m_palette.status_background);
  SetBkMode(device_context, TRANSPARENT);

  m_text_editor.render(*this, device_context, layout);
  m_terminal_panel.render(*this, device_context, layout);
  m_tool_sidebar.render(*this, device_context, layout);
  m_activity_sidebar.render(*this, device_context, layout);
  m_shader_sandbox_panel.render(*this, device_context, layout);
  if (const UI::Editor::TextDocumentModel *document =
          m_text_editor.get_document()) {
    const std::vector<UI::Editor::BreadcrumbItem> full_breadcrumbs =
        document->get_full_breadcrumbs();
    m_footer_toolbar.render(*this, device_context, layout, full_breadcrumbs,
                            document->get_status());
  }
}

void StudioWorkspaceRenderer::fill_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color) const {
  if (rectangle.is_empty()) {
    return;
  }
  RECT native_rectangle = to_native_rect(rectangle);
  SetDCBrushColor(device_context, to_color_ref(color));
  FillRect(device_context, &native_rectangle,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void StudioWorkspaceRenderer::fill_rounded_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color, float radius) const {
  if (rectangle.is_empty()) {
    return;
  }
  int w = round_to_int(rectangle.width);
  int h = round_to_int(rectangle.height);
  if (w <= 0 || h <= 0) {
    return;
  }

  float r = std::min({radius, rectangle.width * 0.5f, rectangle.height * 0.5f});
  if (r <= 0.0f) {
    RECT bounds = to_native_rect(rectangle);
    SetDCBrushColor(device_context, to_color_ref(color));
    FillRect(device_context, &bounds,
             static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC memDC = CreateCompatibleDC(device_context);
  void *bits = nullptr;
  HBITMAP hBmp =
      CreateDIBSection(device_context, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (hBmp) {
    auto *pixels = static_cast<uint32_t *>(bits);
    const uint32_t col_r = color.red;
    const uint32_t col_g = color.green;
    const uint32_t col_b = color.blue;
    const int ri = static_cast<int>(r);
    const float w_minus_r = static_cast<float>(w) - r;
    const float h_minus_r = static_cast<float>(h) - r;

    std::memset(pixels, 0, static_cast<size_t>(w) * h * sizeof(uint32_t));
    const uint32_t opaque_pixel =
        (255u << 24) | (col_r << 16) | (col_g << 8) | col_b;

    for (int y = 0; y < h; ++y) {
      const float cy = static_cast<float>(y) + 0.5f;
      const bool in_corner_row = (y < ri) || (y >= h - ri);
      uint32_t *row = &pixels[(h - 1 - y) * w];

      if (!in_corner_row) {
        std::fill(row, row + w, opaque_pixel);
        continue;
      }

      const float dy = std::max({r - cy, 0.0f, cy - h_minus_r});
      for (int x = 0; x < w; ++x) {
        const float cx = static_cast<float>(x) + 0.5f;
        const bool in_corner_col = (x < ri) || (x >= w - ri);
        if (!in_corner_col) {
          row[x] = opaque_pixel;
          continue;
        }

        const float dx = std::max({r - cx, 0.0f, cx - w_minus_r});
        const float dist = std::sqrt(dx * dx + dy * dy);
        float alpha_f = std::clamp(0.5f - (dist - r), 0.0f, 1.0f);

        if (alpha_f > 0.0f) {
          uint32_t a = static_cast<uint32_t>(alpha_f * 255.0f);
          uint32_t pr = (col_r * a) / 255;
          uint32_t pg = (col_g * a) / 255;
          uint32_t pb = (col_b * a) / 255;
          row[x] = (a << 24) | (pr << 16) | (pg << 8) | pb;
        }
      }
    }

    HGDIOBJ oldBmp = SelectObject(memDC, hBmp);

    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    AlphaBlend(device_context, round_to_int(rectangle.x),
               round_to_int(rectangle.y), w, h, memDC, 0, 0, w, h, bf);

    SelectObject(memDC, oldBmp);
    DeleteObject(hBmp);
  }
  DeleteDC(memDC);
}

void StudioWorkspaceRenderer::draw_rectangle(
    HDC device_context, const UI::Rect &rectangle,
    const UI::Theme::Color &color) const {
  if (rectangle.is_empty()) {
    return;
  }
  const RECT bounds = to_native_rect(rectangle);
  HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
  HGDIOBJ previous_pen = SelectObject(device_context, pen);
  HGDIOBJ previous_brush =
      SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
  Rectangle(device_context, bounds.left, bounds.top, bounds.right,
            bounds.bottom);
  SelectObject(device_context, previous_brush);
  SelectObject(device_context, previous_pen);
  DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_line(HDC device_context, int from_x,
                                        int from_y, int to_x, int to_y,
                                        const UI::Theme::Color &color) const {
  HPEN pen = CreatePen(PS_SOLID, 1, to_color_ref(color));
  HGDIOBJ previous_pen = SelectObject(device_context, pen);
  MoveToEx(device_context, from_x, from_y, nullptr);
  LineTo(device_context, to_x, to_y);
  SelectObject(device_context, previous_pen);
  DeleteObject(pen);
}

void StudioWorkspaceRenderer::draw_text(HDC device_context,
                                        AntialiasedFont &font,
                                        std::string_view text, float point_x,
                                        float center_y,
                                        const UI::Theme::Color &color) const {
  if (text.empty()) {
    return;
  }
  const int baseline =
      round_to_int(center_y -
                   static_cast<float>(font.getAscent(device_context) +
                                      font.getDescent(device_context)) *
                       0.5F +
                   static_cast<float>(font.getAscent(device_context)));
  font.drawString(device_context, to_font_color(color), round_to_int(point_x),
                  baseline, std::string{text});
}

void StudioWorkspaceRenderer::draw_scaled_text(
    HDC device_context, AntialiasedFont &font, std::string_view text,
    float point_x, float center_y, float scale,
    const UI::Theme::Color &color) const {
  if (text.empty() || scale <= 0.0f) {
    return;
  }

  const int baseline =
      round_to_int(center_y -
                   static_cast<float>(font.getAscent(device_context) +
                                      font.getDescent(device_context)) *
                       0.5F +
                   static_cast<float>(font.getAscent(device_context)));

  // Calculate center of the text for scaling
  const float text_width =
      static_cast<float>(font.getTextWidth(device_context, std::string{text}));
  const float text_center_x = point_x + text_width * 0.5f;
  const float text_center_y = center_y;

  // Enable advanced graphics mode for world transform
  int old_graphics_mode = SetGraphicsMode(device_context, GM_ADVANCED);

  // Get the previous transform to restore later
  XFORM old_xform;
  GetWorldTransform(device_context, &old_xform);

  // Setup scaling transform around the center point
  XFORM xform;
  xform.eM11 = scale;
  xform.eM12 = 0.0f;
  xform.eM21 = 0.0f;
  xform.eM22 = scale;
  xform.eDx = text_center_x * (1.0f - scale);
  xform.eDy = text_center_y * (1.0f - scale);

  // Apply the scaling transform relative to any existing transform
  ModifyWorldTransform(device_context, &xform, MWT_RIGHTMULTIPLY);

  // Draw the text (it will be scaled by the transform)
  font.drawString(device_context, to_font_color(color), round_to_int(point_x),
                  baseline, std::string{text});

  // Restore previous transform and graphics mode
  SetWorldTransform(device_context, &old_xform);
  SetGraphicsMode(device_context, old_graphics_mode);
}

void StudioWorkspaceRenderer::draw_svg_icon(
    HDC device_context, std::string_view asset_name, int center_x, int center_y,
    int size, const UI::Theme::Color &color, const UI::Theme::Color &background,
    bool preserve_source_colors) const {
  if (device_context == nullptr || size <= 0 || asset_name.empty()) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_name};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
    if (std::filesystem::is_regular_file(themed_path, path_error)) {
      resolved_path = themed_path;
    } else {
      // Keep compatibility with callers that pass Assets/icons/foo.svg.
      const std::filesystem::path legacy_path =
          m_icon_asset_root / resolved_path.filename();
      if (std::filesystem::is_regular_file(legacy_path, path_error)) {
        resolved_path = legacy_path;
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }
  preserve_source_colors =
      preserve_source_colors &&
      resolved_path.parent_path().filename() == "material-icon-theme";

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@" + std::to_string(size) +
                                "#" + to_font_color(color) + "/" +
                                to_font_color(background);
  auto cached = m_svg_cache.find(cache_key);
  if (cached == m_svg_cache.end()) {
    auto document = lunasvg::Document::loadFromFile(resolved_string);
    if (!document) {
      return;
    }
    auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size),
                                           static_cast<std::uint32_t>(size));
    if (bitmap.isNull()) {
      return;
    }

    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size) *
                                      static_cast<std::size_t>(size));
    const auto *source = reinterpret_cast<const std::uint32_t *>(bitmap.data());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const std::uint32_t alpha = (source[index] >> 24U) & 0xFFU;
      const std::uint32_t inverse_alpha = 255U - alpha;
      const std::uint32_t source_red = (source[index] >> 16U) & 0xFFU;
      const std::uint32_t source_green = (source[index] >> 8U) & 0xFFU;
      const std::uint32_t source_blue = source[index] & 0xFFU;
      const std::uint32_t red =
          preserve_source_colors
              ? source_red + (static_cast<std::uint32_t>(background.red) *
                              inverse_alpha) /
                                 255U
              : (static_cast<std::uint32_t>(color.red) * alpha +
                 static_cast<std::uint32_t>(background.red) * inverse_alpha) /
                    255U;
      const std::uint32_t green =
          preserve_source_colors
              ? source_green + (static_cast<std::uint32_t>(background.green) *
                                inverse_alpha) /
                                   255U
              : (static_cast<std::uint32_t>(color.green) * alpha +
                 static_cast<std::uint32_t>(background.green) * inverse_alpha) /
                    255U;
      const std::uint32_t blue =
          preserve_source_colors
              ? source_blue + (static_cast<std::uint32_t>(background.blue) *
                               inverse_alpha) /
                                  255U
              : (static_cast<std::uint32_t>(color.blue) * alpha +
                 static_cast<std::uint32_t>(background.blue) * inverse_alpha) /
                    255U;
      pixels[index] = blue | (green << 8U) | (red << 16U);
    }
    cached = m_svg_cache.emplace(cache_key, std::move(pixels)).first;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = size;
  bitmap_info.bmiHeader.biHeight = -size;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  const int half = size / 2;
  SetDIBitsToDevice(device_context, center_x - half, center_y - half,
                    static_cast<DWORD>(size), static_cast<DWORD>(size), 0, 0, 0,
                    static_cast<UINT>(size), cached->second.data(),
                    &bitmap_info, DIB_RGB_COLORS);
}

void StudioWorkspaceRenderer::draw_png_icon(
    HDC device_context, const std::string &asset_path, int center_x,
    int center_y, int max_size, const UI::Theme::Color &background) const {
  if (device_context == nullptr || max_size <= 0 || asset_path.empty()) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_path};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
    if (std::filesystem::is_regular_file(themed_path, path_error)) {
      resolved_path = themed_path;
    } else {
      const std::filesystem::path legacy_path =
          m_icon_asset_root / resolved_path.filename();
      if (std::filesystem::is_regular_file(legacy_path, path_error)) {
        resolved_path = legacy_path;
      } else {
        const std::filesystem::path root_path =
            m_icon_asset_root.parent_path().parent_path() / resolved_path;
        if (std::filesystem::is_regular_file(root_path, path_error)) {
          resolved_path = root_path;
        }
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    auto search_dir = std::filesystem::current_path(path_error);
    for (int i = 0; i < 6 && !search_dir.empty(); ++i) {
      const auto candidate = search_dir / "Assets" / "icons" /
                             std::filesystem::path(asset_path).filename();
      if (std::filesystem::is_regular_file(candidate, path_error)) {
        resolved_path = candidate;
        break;
      }
      const auto candidate_direct = search_dir / asset_path;
      if (std::filesystem::is_regular_file(candidate_direct, path_error)) {
        resolved_path = candidate_direct;
        break;
      }
      if (search_dir == search_dir.parent_path())
        break;
      search_dir = search_dir.parent_path();
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@png#" +
                                std::to_string(max_size) + "/" +
                                to_font_color(background);

  auto it = m_svg_cache.find(cache_key);
  if (it == m_svg_cache.end()) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data =
        stbi_load(resolved_string.c_str(), &width, &height, &channels, 4);
    if (!data) {
      return;
    }

    int draw_w = width;
    int draw_h = height;
    if (width > max_size || height > max_size) {
      float aspect = static_cast<float>(width) / static_cast<float>(height);
      if (width > height) {
        draw_w = max_size;
        draw_h = std::max(1, static_cast<int>(max_size / aspect));
      } else {
        draw_h = max_size;
        draw_w = std::max(1, static_cast<int>(max_size * aspect));
      }
    }

    // High quality area-averaging supersampling to eliminate pixelation and
    // jagged edges
    auto resampled = Utility::AntialiasedImage::resample_area_average(
        data, width, height, draw_w, draw_h);

    stbi_image_free(data);

    Utility::ColorRGBA bg_color{background.red, background.green,
                                background.blue, 255};
    auto bmp_data = Utility::AntialiasedImage::composite_to_dib(
        resampled, draw_w, draw_h, max_size, bg_color);

    auto emplaced = m_svg_cache.emplace(cache_key, std::move(bmp_data));
    it = emplaced.first;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = max_size;
  bitmap_info.bmiHeader.biHeight = max_size;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const int half = max_size / 2;
  SetDIBitsToDevice(device_context, center_x - half, center_y - half,
                    static_cast<DWORD>(max_size), static_cast<DWORD>(max_size),
                    0, 0, 0, static_cast<UINT>(max_size), it->second.data(),
                    &bitmap_info, DIB_RGB_COLORS);
}

int StudioWorkspaceRenderer::get_text_width(HDC device_context,
                                            AntialiasedFont &font,
                                            std::string_view text) const {
  return font.getTextWidth(device_context, std::string{text});
}

std::optional<std::filesystem::path>
StudioWorkspaceRenderer::handle_right_click(float point_x, float point_y,
                                            int client_width, int client_height,
                                            float content_top) {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.handle_right_click(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_prompt_modal_visible() const noexcept {
  return m_prompt_modal.is_visible();
}

void StudioWorkspaceRenderer::render_prompt_modal(HDC device_context,
                                                  int client_width,
                                                  int client_height) const {
  if (!m_prompt_modal.is_visible()) {
    return;
  }

  const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                          static_cast<float>(client_height)};
  const auto layout = m_prompt_modal.calculate_layout(viewport, m_dpi_scale);

  // 1. Subtle semi-transparent backdrop overlay (No solid pitch-black)
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 70;
  blend.AlphaFormat = 0;

  HDC mem_dc = CreateCompatibleDC(device_context);
  HBITMAP mem_bm = CreateCompatibleBitmap(device_context, 1, 1);
  HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);
  SetPixel(mem_dc, 0, 0, RGB(0, 0, 0));
  AlphaBlend(device_context,
             static_cast<int>(layout.base_layout.backdrop_bounds.x),
             static_cast<int>(layout.base_layout.backdrop_bounds.y),
             static_cast<int>(layout.base_layout.backdrop_bounds.width),
             static_cast<int>(layout.base_layout.backdrop_bounds.height),
             mem_dc, 0, 0, 1, 1, blend);
  SelectObject(mem_dc, old_bm);
  DeleteObject(mem_bm);
  DeleteDC(mem_dc);

  // 2. Dialog Container (VS Code sleek dark card)
  const UI::Theme::Color dialog_bg{30, 30, 34, 255};
  const UI::Theme::Color border_col{60, 64, 75, 255};

  fill_rounded_rectangle(device_context, layout.base_layout.dialog_bounds,
                         dialog_bg, 6.0F * m_dpi_scale);
  draw_rectangle(device_context, layout.base_layout.dialog_bounds, border_col);

  // 3. Title & Subtitle
  draw_text(device_context, *m_ui_font, m_prompt_modal.get_title(),
            layout.title_bounds.x,
            layout.title_bounds.y + layout.title_bounds.height * 0.5F,
            UI::Theme::Color{255, 255, 255, 255});
  draw_text(device_context, *m_small_font, m_prompt_modal.get_subtitle(),
            layout.subtitle_bounds.x,
            layout.subtitle_bounds.y + layout.subtitle_bounds.height * 0.5F,
            UI::Theme::Color{160, 160, 170, 255});

  // 4. Close (X) button
  const auto close_bg = m_prompt_modal.is_close_hovered()
                            ? UI::Theme::Color{232, 17, 35, 255}
                            : dialog_bg;
  if (m_prompt_modal.is_close_hovered()) {
    fill_rounded_rectangle(device_context, layout.close_button_bounds, close_bg,
                           3.0F * m_dpi_scale);
  }
  draw_text(
      device_context, *m_ui_font, "x",
      layout.close_button_bounds.x + layout.close_button_bounds.width * 0.3F,
      layout.close_button_bounds.y + layout.close_button_bounds.height * 0.5F,
      UI::Theme::Color{200, 200, 200, 255});

  // 5. Input field (if not ConfirmDelete)
  if (m_prompt_modal.get_mode() != UI::Components::PromptMode::ConfirmDelete) {
    const auto &input = m_prompt_modal.get_input();
    const UI::Theme::Color input_bg{20, 20, 24, 255};
    const UI::Theme::Color input_border{0, 122, 204, 255}; // Accent Blue Border
    fill_rounded_rectangle(device_context, layout.input_bounds, input_bg,
                           3.0F * m_dpi_scale);
    draw_rectangle(device_context, layout.input_bounds, input_border);

    const std::string &text = input.get_text();
    const float text_x = layout.input_bounds.x + 8.0F * m_dpi_scale;
    const float text_y =
        layout.input_bounds.y + layout.input_bounds.height * 0.5F;

    if (text.empty()) {
      draw_text(device_context, *m_ui_font, input.get_placeholder(), text_x,
                text_y, UI::Theme::Color{120, 120, 130, 255});
    } else {
      draw_text(device_context, *m_editor_font, text, text_x, text_y,
                UI::Theme::Color{240, 240, 245, 255});
    }

    // Draw Caret
    const int text_w = get_text_width(device_context, *m_editor_font, text);
    const float caret_x = text_x + static_cast<float>(text_w);
    draw_line(device_context, round_to_int(caret_x),
              round_to_int(layout.input_bounds.y + 5.0F * m_dpi_scale),
              round_to_int(caret_x),
              round_to_int(layout.input_bounds.bottom() - 5.0F * m_dpi_scale),
              UI::Theme::Color{255, 255, 255, 255});
  }

  // 6. Cancel Button
  const auto cancel_bg = m_prompt_modal.is_cancel_hovered()
                             ? UI::Theme::Color{55, 55, 62, 255}
                             : UI::Theme::Color{45, 45, 50, 255};
  fill_rounded_rectangle(device_context, layout.cancel_button_bounds, cancel_bg,
                         3.0F * m_dpi_scale);
  draw_rectangle(device_context, layout.cancel_button_bounds, border_col);
  draw_text(device_context, *m_ui_font, "Cancel",
            layout.cancel_button_bounds.x + 22.0F * m_dpi_scale,
            layout.cancel_button_bounds.y +
                layout.cancel_button_bounds.height * 0.5F,
            UI::Theme::Color{220, 220, 220, 255});

  // 7. OK / Confirm Button
  UI::Theme::Color ok_bg =
      (m_prompt_modal.get_mode() == UI::Components::PromptMode::ConfirmDelete)
          ? (m_prompt_modal.is_ok_hovered()
                 ? UI::Theme::Color{232, 17, 35, 255}
                 : UI::Theme::Color{180, 20, 30, 255})
          : (m_prompt_modal.is_ok_hovered()
                 ? UI::Theme::Color{0, 122, 204, 255}
                 : UI::Theme::Color{14, 99, 156, 255});
  fill_rounded_rectangle(device_context, layout.ok_button_bounds, ok_bg,
                         3.0F * m_dpi_scale);
  draw_text(device_context, *m_ui_font, m_prompt_modal.get_confirm_label(),
            layout.ok_button_bounds.x + 24.0F * m_dpi_scale,
            layout.ok_button_bounds.y + layout.ok_button_bounds.height * 0.5F,
            UI::Theme::Color{255, 255, 255, 255});
}

void StudioWorkspaceRenderer::render_add_item_dialog(
    HDC /*device_context*/, int /*client_width*/, int /*client_height*/,
    const UI::Theme::StudioTheme &/*theme*/) const {
  // AddNewItemDialog renders as a standalone native Win32 window (HWND)
}

} // namespace Zenvra::Platform::Win32::Components
