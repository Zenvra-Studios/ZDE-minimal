#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Commands/CommandIds.h"
#include "Language/LanguageServerManager.h"
#include "Platform/HostSystem.h"
#include "Plugins/PluginManager.h"
#include "Settings/SettingsService.h"
#include "Utility/Antialiasing.h"
#include "Utility/IcoDecoder.h"
#include "Utility/stb_image.h"

#include "UI/Editor/EditorFileSystem.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"
#include "Utility/X11Rounded.h"
#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fontconfig/fontconfig.h>

namespace Zenvra::Platform::X11::Components {

namespace {

using Zenvra::Utility::round_to_int;

std::string to_xft_color(const UI::Theme::Color &color) {
  char value[8]{};
  std::snprintf(value, sizeof(value), "#%02x%02x%02x",
                static_cast<unsigned int>(color.red),
                static_cast<unsigned int>(color.green),
                static_cast<unsigned int>(color.blue));
  return value;
}

} // namespace

StudioWorkspaceRenderer::StudioWorkspaceRenderer() = default;

StudioWorkspaceRenderer::~StudioWorkspaceRenderer() { shutdown(); }

bool StudioWorkspaceRenderer::initialize(Display *display, int screen,
                                         float dpi_scale) {
  shutdown();
  m_display = display;
  m_screen = screen;
  m_dpi_scale = std::max(dpi_scale, 0.5F);
  if (m_display == nullptr) {
    return false;
  }

  std::error_code path_error;
  std::filesystem::path exe_dir;
  const std::filesystem::path executable_path =
      std::filesystem::canonical("/proc/self/exe", path_error);
  if (!path_error) {
    exe_dir = executable_path.parent_path();
  }

  path_error.clear();
  const std::filesystem::path current_path =
      std::filesystem::current_path(path_error);
  std::optional<std::filesystem::path> project_root;
  if (!path_error) {
    project_root =
        UI::Editor::EditorFileSystem::find_project_root(current_path);
  }
  if (!project_root && !exe_dir.empty()) {
    project_root =
        UI::Editor::EditorFileSystem::find_project_root(exe_dir);
  }

  // 1. Resolve Icon Directory (prioritizes project_root/Assets/icons then local/installed Resources/icons)
  const std::vector<std::filesystem::path> icon_candidates = {
      project_root ? (*project_root / "Assets" / "icons") : std::filesystem::path{},
      project_root ? (*project_root / "Resources" / "icons") : std::filesystem::path{},
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir / "Resources" / "icons"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir / "Assets" / "icons"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path() / "Resources" / "icons"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path() / "Assets" / "icons"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path().parent_path() / "Resources" / "icons"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path().parent_path() / "Assets" / "icons"),
      current_path / "Assets" / "icons",
      current_path / "Resources" / "icons",
      std::filesystem::path{"/usr/share/zde/Assets/icons"},
      std::filesystem::path{"/usr/share/zde/Resources/icons"},
      std::filesystem::path{"/usr/local/share/zde/Assets/icons"},
      std::filesystem::path{"/usr/local/share/zde/Resources/icons"},
  };
  for (const auto &candidate : icon_candidates) {
    if (!candidate.empty() && std::filesystem::is_directory(candidate, path_error)) {
      m_icon_asset_root = candidate;
      break;
    }
  }

  m_graphics_context =
      XCreateGC(m_display, RootWindow(m_display, m_screen), 0, nullptr);
  if (m_graphics_context == nullptr) {
    shutdown();
    return false;
  }

  // 2. Resolve Fonts Directory
  std::filesystem::path fonts_dir;
  const std::vector<std::filesystem::path> font_candidates = {
      project_root ? (*project_root / "Assets" / "fonts") : std::filesystem::path{},
      project_root ? (*project_root / "Resources" / "fonts") : std::filesystem::path{},
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir / "Resources" / "fonts"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir / "Assets" / "fonts"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path() / "Resources" / "fonts"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path() / "Assets" / "fonts"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path().parent_path() / "Resources" / "fonts"),
      exe_dir.empty() ? std::filesystem::path{} : (exe_dir.parent_path().parent_path() / "Assets" / "fonts"),
      current_path / "Assets" / "fonts",
      current_path / "Resources" / "fonts",
      std::filesystem::path{"/usr/share/zde/Assets/fonts"},
      std::filesystem::path{"/usr/share/zde/Resources/fonts"},
      std::filesystem::path{"/usr/local/share/zde/Assets/fonts"},
      std::filesystem::path{"/usr/local/share/zde/Resources/fonts"},
  };
  for (const auto &candidate : font_candidates) {
    if (!candidate.empty() && std::filesystem::is_directory(candidate, path_error)) {
      fonts_dir = candidate;
      break;
    }
  }

  // Load bundled fonts from fonts_dir into Fontconfig
  if (!fonts_dir.empty() && std::filesystem::is_directory(fonts_dir, path_error)) {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(
             fonts_dir,
             std::filesystem::directory_options::skip_permission_denied,
             path_error)) {
      if (!path_error && entry.is_regular_file(path_error) &&
          entry.path().extension() == ".ttf") {
        FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(
                                            entry.path().string().c_str()));
      }
    }
  }

  const char *editor_font_family = "Hack, JetBrainsMono Nerd Font, JetBrains Mono, monospace";
  const char *ui_font_family = "Open Sans, Adwaita Sans, Inter, Cantarell, sans-serif";

  char ui_pattern[256]{};
  char small_pattern[256]{};
  char editor_pattern[256]{};
  char minimap_pattern[256]{};
  char large_pattern[256]{};

  std::snprintf(
      ui_pattern, sizeof(ui_pattern),
      "%s:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
      ui_font_family, std::max(round_to_int(13.0F * m_dpi_scale), 10));
  std::snprintf(
      small_pattern, sizeof(small_pattern),
      "%s:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
      ui_font_family, std::max(round_to_int(11.5F * m_dpi_scale), 9));
  std::snprintf(
      editor_pattern, sizeof(editor_pattern),
      "%s:pixelsize=%d:antialias=true:hinting=true:hintstyle=hintslight",
      editor_font_family, std::max(round_to_int(14.0F * m_dpi_scale), 11));
  std::snprintf(
      minimap_pattern, sizeof(minimap_pattern),
      "%s:pixelsize=%d:antialias=true:hinting=false",
      editor_font_family, std::max(round_to_int(3.0F * m_dpi_scale), 3));
  std::snprintf(
      large_pattern, sizeof(large_pattern),
      "%s:pixelsize=%d:weight=bold:antialias=true:hinting=true:hintstyle=hintslight",
      ui_font_family, std::max(round_to_int(22.0F * m_dpi_scale), 16));

  m_ui_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, ui_pattern);
  m_small_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, small_pattern);
  m_editor_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, editor_pattern);
  m_minimap_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, minimap_pattern);
  m_large_font =
      std::make_unique<AntialiasedFont>(m_display, m_screen, large_pattern);

  if (m_ui_font->getHeight() <= 0 || m_small_font->getHeight() <= 0 ||
      m_editor_font->getHeight() <= 0 || m_minimap_font->getHeight() <= 0 ||
      m_large_font->getHeight() <= 0) {
    shutdown();
    return false;
  }

  m_pixels.workspace_background =
      allocate_color(m_palette.workspace_background);
  m_pixels.tab_background = allocate_color(m_palette.tab_background);
  m_pixels.tab_active_background =
      allocate_color(m_palette.tab_active_background);
  m_pixels.sidebar_background = allocate_color(m_palette.sidebar_background);
  m_pixels.editor_background = allocate_color(m_palette.editor_background);
  m_pixels.active_line_background =
      allocate_color(m_palette.active_line_background);
  m_pixels.selection_background =
      allocate_color(m_palette.selection_background);
  m_pixels.status_background = allocate_color(m_palette.status_background);
  m_pixels.border = allocate_color(m_palette.border);
  m_pixels.text_primary = allocate_color(m_palette.text_primary);
  m_pixels.text_muted = allocate_color(m_palette.text_muted);
  m_pixels.accent = allocate_color(m_palette.accent);
  m_pixels.warning = allocate_color(m_palette.warning);
  m_pixels.success = allocate_color(m_palette.success);
  m_pixels.hover_background = allocate_color(m_palette.hover_background);
  m_pixels.indent_guide = allocate_color(m_palette.indent_guide);
  m_pixels.indent_guide_active = allocate_color(m_palette.indent_guide_active);
  m_text.primary = to_xft_color(m_palette.text_primary);
  m_text.muted = to_xft_color(m_palette.text_muted);
  m_text.keyword = to_xft_color(m_palette.keyword);
  m_text.number = to_xft_color(m_palette.number);
  m_text.label = to_xft_color(m_palette.label);
  m_text.type = to_xft_color(m_palette.type);
  m_text.comment = to_xft_color(m_palette.comment);
  m_text.accent = to_xft_color(m_palette.accent);
  m_text.warning = to_xft_color(m_palette.warning);
  m_text.success = to_xft_color(m_palette.success);

  m_text_dimmed.primary = to_xft_color(UI::Theme::dim_color(m_palette.text_primary, m_palette.editor_background));
  m_text_dimmed.muted = to_xft_color(UI::Theme::dim_color(m_palette.text_muted, m_palette.editor_background));
  m_text_dimmed.keyword = to_xft_color(UI::Theme::dim_color(m_palette.keyword, m_palette.editor_background));
  m_text_dimmed.number = to_xft_color(UI::Theme::dim_color(m_palette.number, m_palette.editor_background));
  m_text_dimmed.label = to_xft_color(UI::Theme::dim_color(m_palette.label, m_palette.editor_background));
  m_text_dimmed.type = to_xft_color(UI::Theme::dim_color(m_palette.type, m_palette.editor_background));
  m_text_dimmed.comment = to_xft_color(UI::Theme::dim_color(m_palette.comment, m_palette.editor_background));
  m_text_dimmed.accent = to_xft_color(UI::Theme::dim_color(m_palette.accent, m_palette.editor_background));
  m_text_dimmed.warning = to_xft_color(UI::Theme::dim_color(m_palette.warning, m_palette.editor_background));
  m_text_dimmed.success = to_xft_color(UI::Theme::dim_color(m_palette.success, m_palette.editor_background));
  static_cast<void>(m_tool_sidebar.initialize());
  const auto active_workspace = m_tool_sidebar.get_model().get_workspace_root();
  if (!active_workspace.empty()) {
    m_terminal_panel.set_working_directory(active_workspace);
  } else {
    m_terminal_panel.set_working_directory(Platform::HostSystem::get_user_home_directory());
  }
  static_cast<void>(m_shader_sandbox_panel.initialize());
  return true;
}

void StudioWorkspaceRenderer::sync_shader_sandbox() const {
  if (!m_shader_sandbox_panel.is_visible()) {
    return;
  }
  if (const UI::Editor::TextDocumentModel *doc =
          m_text_editor.get_document()) {
    const std::string filename = std::string(doc->get_file_name());
    const std::filesystem::path file_path(filename);
    const std::string ext = file_path.extension().string();

    std::string full_text;
    for (const auto &line : doc->get_lines()) {
      full_text += line;
      full_text += '\n';
    }

    const bool is_shader_ext = (ext == ".glsl" || ext == ".frag" || ext == ".vert" || 
                                ext == ".comp" || ext == ".shader" || ext == ".hlsl" ||
                                ext == ".geom" || ext == ".tesc" || ext == ".tese");
    const bool is_shader_content = (full_text.find("mainImage") != std::string::npos ||
                                    full_text.find("gl_FragColor") != std::string::npos ||
                                    full_text.find("gl_FragCoord") != std::string::npos ||
                                    full_text.find("#version") != std::string::npos);

    if ((is_shader_ext || is_shader_content) && !full_text.empty()) {
      m_shader_sandbox_panel.set_source_code(full_text);
    }
  }
}

bool StudioWorkspaceRenderer::open_file(const std::filesystem::path &path) {
  const bool opened = m_text_editor.open_file(path);
  if (opened) {
    const std::string ext = path.extension().string();
    if (ext == ".glsl" || ext == ".frag" || ext == ".vert" || ext == ".comp" ||
        ext == ".shader" || ext == ".hlsl") {
      m_shader_sandbox_panel.set_visible(true);
    }
    sync_shader_sandbox();
  }
  return opened;
}

bool StudioWorkspaceRenderer::open_file_at_location(
    const std::filesystem::path &path,
    std::size_t line,
    std::size_t column) {
  const bool opened = m_text_editor.open_file_at_location(path, line, column);
  if (opened) {
    m_terminal_panel.set_focused(false);
    const std::string ext = path.extension().string();
    if (ext == ".glsl" || ext == ".frag" || ext == ".vert") {
      m_shader_sandbox_panel.set_visible(true);
    }
    sync_shader_sandbox();
  }
  return opened;
}

bool StudioWorkspaceRenderer::set_workspace_root(
    const std::filesystem::path &root) {
  static_cast<void>(m_text_editor.close_all_files());
  if (!m_tool_sidebar.set_workspace_root(root)) {
    return false;
  }
  m_terminal_panel.set_working_directory(root);
  Language::LanguageServerManager::instance().set_workspace_root(root);
  return true;
}

bool StudioWorkspaceRenderer::close_project() {
  static_cast<void>(m_text_editor.close_all_files());
  m_tool_sidebar.clear_workspace();
  m_terminal_panel.shutdown();
  m_terminal_panel.set_working_directory(Platform::HostSystem::get_user_home_directory());
  m_shader_sandbox_panel.set_visible(false);
  Language::LanguageServerManager::instance().shutdown_all();
  Language::LanguageServerManager::instance().set_workspace_root({});
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
    float point_x, float point_y, int client_width, int client_height,
    float content_top, bool extend_selection, int click_count, Time event_time,
    std::string &command_out, bool is_control_down) {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  // Tool switcher popup has priority (ported from Win32): consume clicks
  // inside, switch tool via PluginManager, dismiss on outside click.
  if (m_tool_switcher_popup.is_visible()) {
    const auto pop_res = m_tool_switcher_popup.handle_pointer_press(
        point_x, point_y, m_dpi_scale);
    if (pop_res.handled) {
      if (!pop_res.switched_tool_id.empty()) {
        auto& pm = Zenvra::Plugins::PluginManager::instance();
        pm.set_active_tool_plugin_id(pop_res.switched_tool_id);
        if (auto active_tool = pm.get_active_tool_plugin()) {
          UI::Editor::set_active_tool_sidebar_item(
              active_tool->get_id(), active_tool->get_name());
        }
      } else if (pop_res.open_marketplace) {
        command_out = "zde.marketplace.open";
      }
      return true;
    }
    m_tool_switcher_popup.hide();
    return true;
  }
  // Settings overlay has priority next (ported from Win32): forward to
  // SettingsWindow layout/interaction. Covers close button, tabs, search,
  // sidebar categories and rows.
  if (m_settings_window.is_visible()) {
    const auto settings_layout = m_settings_window.calculate_layout(
        static_cast<float>(client_width), static_cast<float>(client_height),
        m_dpi_scale);
    if (m_settings_window.handle_pointer_press(
            point_x, point_y, settings_layout)) {
      return true;
    }
    // Click outside dialog dismisses (matches Win32 overlay behavior).
    if (!settings_layout.dialog_bounds.contains(point_x, point_y)) {
      m_settings_window.close();
      return true;
    }
    return true;
  }
  if (const std::optional<std::size_t> sidebar_index =
          UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y)) {
    const std::span<const UI::Editor::SidebarItem> items =
        UI::Editor::get_studio_sidebar_items();
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Terminal) {
      if (!m_terminal_panel.is_visible()) {
        m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Terminal);
        return m_terminal_panel.toggle();
      }
      if (m_terminal_panel.get_active_channel() != TerminalPanel::PanelChannel::Terminal) {
        m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Terminal);
        m_terminal_panel.set_focused(true);
        return true;
      }
      return m_terminal_panel.toggle();
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::Shader) {
      const bool res = m_shader_sandbox_panel.toggle();
      if (res) {
        sync_shader_sandbox();
      }
      return true;
    }
    if (items[*sidebar_index].icon == UI::Editor::SidebarIcon::More) {
      const UI::Rect item_bounds =
          UI::Editor::calculate_studio_sidebar_item_bounds(layout, *sidebar_index);
      m_tool_switcher_popup.show(
          item_bounds.right() + 4.0F * m_dpi_scale, item_bounds.y);
      return true;
    }
    return m_tool_sidebar.activate(items[*sidebar_index].icon);
  }
  if (m_shader_sandbox_panel.handle_pointer_press(layout, point_x, point_y)) {
    return true;
  }
  const auto sidebar_result =
      m_tool_sidebar.handle_pointer_press(layout, point_x, point_y);
  if (sidebar_result.handled) {
    m_terminal_panel.set_focused(false);
    if (sidebar_result.action == SidebarActionKind::OpenFile && sidebar_result.path) {
      if (sidebar_result.path->string() == "::OPEN_FOLDER::") {
        command_out = "zde.project.open";
      } else if (sidebar_result.line.has_value() || sidebar_result.column.has_value()) {
        static_cast<void>(open_file_at_location(
            *sidebar_result.path,
            sidebar_result.line.value_or(0),
            sidebar_result.column.value_or(0)));
      } else {
        static_cast<void>(open_file(*sidebar_result.path));
      }
    } else if (sidebar_result.action == SidebarActionKind::NewFile) {
      command_out = "zde.explorer.newFile";
    } else if (sidebar_result.action == SidebarActionKind::NewFolder) {
      command_out = "zde.explorer.newFolder";
    } else if (sidebar_result.action == SidebarActionKind::Refresh) {
      command_out = "zde.explorer.refresh";
    } else if (sidebar_result.action == SidebarActionKind::CollapseAll) {
      command_out = "zde.explorer.collapseAll";
    } else if (sidebar_result.action == SidebarActionKind::CloneRepository) {
      command_out = "zde.git.clone";
    }
    return true;
  }
  if (m_terminal_panel.is_visible() &&
      m_terminal_panel.handle_pointer_press(layout, point_x, point_y,
                                            event_time, click_count,
                                            extend_selection)) {
    m_tool_sidebar.set_focused(false);
    return true;
  }
  if (m_shader_sandbox_panel.is_visible() &&
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    m_tool_sidebar.set_focused(false);
    return true;
  }
  m_terminal_panel.set_focused(false);
  m_tool_sidebar.set_focused(false);
  const bool editor_pressed = m_text_editor.handle_pointer_press(
      *this, layout, point_x, point_y, extend_selection, click_count,
      command_out, is_control_down);
  if (editor_pressed) {
    sync_shader_sandbox();
  }
  return editor_pressed;
}

bool StudioWorkspaceRenderer::handle_pointer_move(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  const bool sidebar_changed =
      m_tool_sidebar.handle_pointer_move(layout, point_x, point_y);
  const bool terminal_changed =
      m_terminal_panel.handle_pointer_move(layout, point_x, point_y);
  const bool editor_changed =
      m_text_editor.handle_pointer_move(layout, point_x, point_y);
  const bool shader_changed =
      m_shader_sandbox_panel.handle_pointer_move(layout, point_x, point_y);
  bool popup_changed = false;
  if (m_tool_switcher_popup.is_visible()) {
    popup_changed = m_tool_switcher_popup.handle_pointer_move(
        point_x, point_y, m_dpi_scale);
  }
  bool settings_changed = false;
  if (m_settings_window.is_visible()) {
    const auto settings_layout = m_settings_window.calculate_layout(
        static_cast<float>(client_width), static_cast<float>(client_height),
        m_dpi_scale);
    settings_changed = m_settings_window.handle_pointer_move(
        point_x, point_y, settings_layout);
  }
  return sidebar_changed || terminal_changed || editor_changed ||
         shader_changed || popup_changed || settings_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) {
  if (m_settings_window.is_visible()) {
    const auto settings_layout = m_settings_window.calculate_layout(
        static_cast<float>(client_width), static_cast<float>(client_height),
        m_dpi_scale);
    if (m_settings_window.is_dragging_scrollbar() ||
        m_settings_window.is_dragging_sidebar_scrollbar() ||
        m_settings_window.get_dropdown().is_dragging_scrollbar()) {
      return m_settings_window.handle_pointer_move(point_x, point_y, settings_layout);
    }
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_text_editor.is_pointer_selecting() || m_text_editor.is_resizing_split() ||
      m_text_editor.is_tab_dragging() || m_text_editor.is_scrollbar_dragging()) {
    return m_text_editor.handle_pointer_drag(*this, layout, point_x, point_y);
  }
  if (m_tool_sidebar.is_resizing() || m_tool_sidebar.is_dragging_item() ||
      m_tool_sidebar.is_dragging_scrollbar()) {
    return m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_terminal_panel.is_resizing() || m_terminal_panel.is_selecting_text()) {
    return m_terminal_panel.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_shader_sandbox_panel.is_resizing()) {
    return m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_tool_sidebar.is_visible() && m_tool_sidebar.contains(layout, point_x, point_y)) {
    if (m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  if (m_terminal_panel.is_visible() && m_terminal_panel.contains(layout, point_x, point_y)) {
    if (m_terminal_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  if (m_shader_sandbox_panel.is_visible() &&
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    if (m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  return m_text_editor.handle_pointer_drag(*this, layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept {
  bool settings_changed = false;
  if (m_settings_window.is_visible()) {
    settings_changed = m_settings_window.handle_pointer_release(0.0F, 0.0F, {});
  }
  const bool terminal_changed = m_terminal_panel.handle_pointer_release();
  const bool sidebar_changed = m_tool_sidebar.handle_pointer_release();
  const bool editor_changed = m_text_editor.handle_pointer_release();
  const bool shader_changed = m_shader_sandbox_panel.handle_pointer_release();
  return settings_changed || terminal_changed || sidebar_changed || editor_changed ||
         shader_changed;
}

bool StudioWorkspaceRenderer::handle_scroll(float point_x, float point_y,
                                            std::string &command_out,
                                            std::ptrdiff_t line_delta,
                                            bool horizontal, int client_width,
                                            int client_height,
                                            float content_top) noexcept {
  if (m_settings_window.is_visible()) {
    const auto settings_layout = m_settings_window.calculate_layout(
        static_cast<float>(client_width), static_cast<float>(client_height),
        m_dpi_scale);
    m_settings_window.handle_scroll(static_cast<float>(line_delta), settings_layout, point_x, point_y);
    return true;
  }
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.handle_scroll(*this, layout, point_x, point_y,
                                     command_out, line_delta, horizontal);
}

bool StudioWorkspaceRenderer::handle_editor_input(
    UI::Editor::EditorInputCommand command, bool extend_selection) {
  const bool res = m_text_editor.handle_input(command, extend_selection);
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

bool StudioWorkspaceRenderer::handle_editor_action(
    UI::Editor::EditorAction action) {
  const bool res = m_text_editor.handle_action(action);
  if (res) {
    sync_shader_sandbox();
  }
  return res;
}

void StudioWorkspaceRenderer::reset_layout() noexcept {
  m_tool_sidebar.get_model().set_visible(true);
  static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
  if (m_terminal_panel.is_visible()) {
    static_cast<void>(m_terminal_panel.toggle());
  }
  m_shader_sandbox_panel.set_visible(false);
  m_text_editor.reset_split();
}

std::optional<bool>
StudioWorkspaceRenderer::handle_editor_command(std::string_view command_id) {
  if (command_id == Commands::CommandIds::window_reset_layout) {
    reset_layout();
    return true;
  }
  if (command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel") {
    const bool res = toggle_shader_panel();
    if (res) {
      sync_shader_sandbox();
    }
    return res;
  }
  if (command_id == Commands::CommandIds::view_terminal_panel) {
    if (!m_terminal_panel.is_visible()) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Terminal);
      return m_terminal_panel.toggle();
    }
    if (m_terminal_panel.get_active_channel() != TerminalPanel::PanelChannel::Terminal) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Terminal);
      m_terminal_panel.set_focused(true);
      return true;
    }
    return m_terminal_panel.toggle();
  }
  if (command_id == Commands::CommandIds::view_output) {
    if (!m_terminal_panel.is_visible()) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Output);
      return m_terminal_panel.toggle();
    }
    if (m_terminal_panel.get_active_channel() != TerminalPanel::PanelChannel::Output) {
      m_terminal_panel.set_active_channel(TerminalPanel::PanelChannel::Output);
      m_terminal_panel.set_focused(true);
      return true;
    }
    return m_terminal_panel.toggle();
  }
  if (command_id == Commands::CommandIds::view_toggle_bottom_dock ||
      command_id == Commands::CommandIds::view_problems ||
      command_id == Commands::CommandIds::view_diagnostics) {
    return toggle_terminal();
  }
  if (command_id == Commands::CommandIds::view_toggle_left_dock) {
    const bool vis = !m_tool_sidebar.is_visible();
    m_tool_sidebar.get_model().set_visible(vis);
    return true;
  }
  if (command_id == Commands::CommandIds::view_explorer ||
      command_id == Commands::CommandIds::view_project_panel ||
      command_id == Commands::CommandIds::view_outline_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Project));
    return true;
  }
  if (command_id == Commands::CommandIds::view_search ||
      command_id == Commands::CommandIds::edit_find_in_project) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Search));
    m_tool_sidebar.get_search_input().set_focused(true);
    m_tool_sidebar.get_search_input().reset_blink();
    m_tool_sidebar.set_focused(true);
    return true;
  }
  if (command_id == Commands::CommandIds::view_git_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::VersionControl));
    return true;
  }
  if (command_id == Commands::CommandIds::view_debugger_panel) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Run));
    return true;
  }
  if (command_id == Commands::CommandIds::open_plugins) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Services));
    return true;
  }
  if (command_id == Commands::CommandIds::open_settings) {
    if (m_open_settings_callback) {
      m_settings_window.close();
      m_open_settings_callback();
      return true;
    }
    if (m_settings_window.is_visible()) {
      m_settings_window.close();
    } else {
      m_settings_window.open();
    }
    return true;
  }
  if (command_id == Commands::CommandIds::open_themes ||
      command_id == Commands::CommandIds::more_tools) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::More));
    return true;
  }
  const auto res = m_text_editor.handle_command(command_id);
  if (res.has_value() && *res) {
    sync_shader_sandbox();
  }
  return res;
}

std::optional<bool> StudioWorkspaceRenderer::is_editor_command_enabled(
    std::string_view command_id) const noexcept {
  if (command_id == Commands::CommandIds::window_reset_layout ||
      command_id == Commands::CommandIds::view_toggle_right_dock ||
      command_id == "zde.view.shaderPanel" ||
      command_id == Commands::CommandIds::view_toggle_bottom_dock ||
      command_id == Commands::CommandIds::view_terminal_panel ||
      command_id == Commands::CommandIds::view_output ||
      command_id == Commands::CommandIds::view_problems ||
      command_id == Commands::CommandIds::view_diagnostics ||
      command_id == Commands::CommandIds::view_toggle_left_dock ||
      command_id == Commands::CommandIds::view_explorer ||
      command_id == Commands::CommandIds::view_project_panel ||
      command_id == Commands::CommandIds::view_outline_panel ||
      command_id == Commands::CommandIds::view_search ||
      command_id == Commands::CommandIds::view_git_panel ||
      command_id == Commands::CommandIds::view_debugger_panel ||
      command_id == Commands::CommandIds::open_plugins ||
      command_id == Commands::CommandIds::open_settings ||
      command_id == Commands::CommandIds::open_themes ||
      command_id == Commands::CommandIds::more_tools ||
      command_id == Commands::CommandIds::edit_profiles) {
    return true;
  }
  return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text) {
  if (m_tool_sidebar.is_search_focused()) {
    return m_tool_sidebar.handle_text_input(utf8_text);
  }
  const bool res = m_terminal_panel.is_focused()
                       ? m_terminal_panel.handle_text_input(utf8_text)
                       : m_text_editor.handle_text_input(utf8_text);
  if (res && !m_terminal_panel.is_focused()) {
    sync_shader_sandbox();
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
    float point_x, float point_y, std::ptrdiff_t line_delta, bool horizontal,
    int client_width, int client_height, float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.handle_scroll(layout, point_x, point_y, line_delta,
                                        horizontal);
}

bool StudioWorkspaceRenderer::handle_terminal_scroll(std::ptrdiff_t line_delta,
                                                     bool horizontal) noexcept {
  return m_terminal_panel.handle_scroll(line_delta, horizontal);
}

bool StudioWorkspaceRenderer::handle_tool_sidebar_scroll(
    std::ptrdiff_t line_delta, int client_width, int client_height,
    float content_top) noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_tool_sidebar.handle_scroll(layout, line_delta);
}

bool StudioWorkspaceRenderer::is_search_focused() const noexcept {
  return m_tool_sidebar.is_search_focused();
}

bool StudioWorkspaceRenderer::handle_search_key(KeySym sym, unsigned int state) {
  return m_tool_sidebar.handle_key(sym, state);
}

bool StudioWorkspaceRenderer::is_editor_focused() const noexcept {
  return !m_terminal_panel.is_focused() && !m_tool_sidebar.is_search_focused() && m_text_editor.is_focused();
}

bool StudioWorkspaceRenderer::is_terminal_focused() const noexcept {
  return m_terminal_panel.is_focused();
}

bool StudioWorkspaceRenderer::is_activity_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (!layout.activity_bar_bounds.contains(point_x, point_y)) {
    return false;
  }
  return UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y).has_value();
}

bool StudioWorkspaceRenderer::is_tab_bar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_ui_font == nullptr) {
    return false;
  }
  return m_text_editor.is_tab_interactive_point(*this, layout, point_x,
                                                point_y);
}

bool StudioWorkspaceRenderer::is_tab_bar_area_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return layout.tab_bar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return (layout.editor_header_bounds.contains(point_x, point_y) ||
          layout.gutter_bounds.contains(point_x, point_y) ||
          layout.editor_bounds.contains(point_x, point_y)) &&
         !layout.minimap_bounds.contains(point_x, point_y) &&
         !layout.scrollbar_bounds.contains(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_scrollbar_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_scrollbar_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_minimap_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_minimap_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_fold_margin_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_fold_margin_point(*this, layout, point_x, point_y);
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

bool StudioWorkspaceRenderer::is_editor_interactive_point(
    float point_x, float point_y) const noexcept {
  return m_text_editor.is_empty_state_interactive_point(point_x, point_y);
}

bool StudioWorkspaceRenderer::is_terminal_interactive_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_terminal_panel.is_interactive_point(layout, point_x, point_y);
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

bool StudioWorkspaceRenderer::is_sidebar_dragging_item() const noexcept {
  return m_tool_sidebar.is_dragging_item();
}

bool StudioWorkspaceRenderer::is_editor_split_resize_handle(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_text_editor.is_split_resize_handle_point(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_editor_split_resizing() const noexcept {
  return m_text_editor.is_split_resizing();
}

bool StudioWorkspaceRenderer::is_shader_panel_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.contains(layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::is_shader_splitter_point(
    float point_x, float point_y, int client_width, int client_height,
    float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  return m_shader_sandbox_panel.is_resize_handle_point(layout, point_x,
                                                       point_y);
}

bool StudioWorkspaceRenderer::is_shader_panel_resizing() const noexcept {
  return m_shader_sandbox_panel.is_resizing();
}

bool StudioWorkspaceRenderer::toggle_shader_panel() noexcept {
  return m_shader_sandbox_panel.toggle();
}

bool StudioWorkspaceRenderer::is_shader_panel_visible() const noexcept {
  return m_shader_sandbox_panel.is_visible();
}

bool StudioWorkspaceRenderer::toggle_terminal() noexcept {
  return m_terminal_panel.toggle();
}

bool StudioWorkspaceRenderer::is_empty_state_button_hovered() const noexcept {
  return m_text_editor.is_empty_state_button_hovered() ||
         m_tool_sidebar.is_empty_state_button_hovered();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept {
  const bool caret_changed = m_text_editor.tick_animations();
  const bool terminal_changed = m_terminal_panel.is_visible() ? m_terminal_panel.poll() : false;
  const bool terminal_blink_changed = m_terminal_panel.is_visible() ? m_terminal_panel.tick_animations() : false;
  const bool shader_changed = m_shader_sandbox_panel.tick_animations();
  const bool prompt_modal_changed = m_prompt_modal.is_visible() ? m_prompt_modal.tick() : false;
  const bool sidebar_changed = m_tool_sidebar.tick_animations();
  return caret_changed || terminal_changed || terminal_blink_changed || shader_changed || prompt_modal_changed || sidebar_changed;
}

void StudioWorkspaceRenderer::shutdown() {
  m_settings_window.close();
  m_terminal_panel.shutdown();
  m_minimap_font.reset();
  m_editor_font.reset();
  m_small_font.reset();
  m_ui_font.reset();
  m_large_font.reset();
  if (m_display != nullptr && m_graphics_context != nullptr) {
    XFreeGC(m_display, m_graphics_context);
  }
  for (auto &[path, image] : m_svg_cache) {
    static_cast<void>(path);
    if (image) {
      XDestroyImage(image);
    }
  }
  m_svg_cache.clear();
  m_icon_asset_root.clear();
  m_graphics_context = nullptr;
  m_display = nullptr;
}

UI::Editor::StudioEditorLayoutResult
StudioWorkspaceRenderer::calculate_layout(int client_width, int client_height,
                                          float content_top) const noexcept {
  const std::size_t line_count = m_text_editor.get_active_document_line_count();
  return m_layout_engine.calculate(
      static_cast<float>(client_width), static_cast<float>(client_height),
      content_top, m_dpi_scale, m_terminal_panel.is_visible(),
      m_terminal_panel.get_height(), m_terminal_panel.is_maximized(),
      m_tool_sidebar.is_visible(), m_tool_sidebar.get_width(),
      m_shader_sandbox_panel.is_visible(), m_shader_sandbox_panel.get_width(),
      std::nullopt, line_count);
}

StudioWorkspaceRenderer::ModernCardGeometry
StudioWorkspaceRenderer::get_modern_card_geometry(
    int client_width, int client_height, float content_top) const noexcept {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  const float scale = layout.dpi_scale;
  const float sep_gap = std::max(std::round(6.0F * scale), 6.0F);
  const float outer_gap = std::max(std::round(5.0F * scale), 4.0F);
  const float card_radius = std::max(std::round(8.0F * scale), 8.0F);

  ModernCardGeometry geom;
  geom.card_radius = card_radius;

  const bool has_sidebar =
      m_tool_sidebar.is_visible() && !layout.tool_sidebar_bounds.is_empty();
  if (has_sidebar) {
    geom.sidebar_card = UI::Rect{
        layout.tool_sidebar_bounds.x + outer_gap,
        layout.tool_sidebar_bounds.y + outer_gap,
        std::max(0.0F, layout.tool_sidebar_bounds.width - outer_gap - sep_gap * 0.5F),
        std::max(0.0F, layout.tool_sidebar_bounds.height - outer_gap * 2.0F),
    };
  }

  const float editor_col_left =
      has_sidebar ? (layout.tool_sidebar_bounds.right() + sep_gap * 0.5F)
                  : (layout.activity_bar_bounds.right() + outer_gap);

  const float shader_split_x =
      layout.shader_splitter_bounds.x + layout.shader_splitter_bounds.width * 0.5F;
  const float editor_col_right =
      layout.shader_panel_visible
          ? (shader_split_x - sep_gap * 0.5F)
          : (layout.workspace_bounds.right() - outer_gap);

  const float editor_col_top = (layout.editor_header_bounds.is_empty()
                                    ? layout.gutter_bounds.y
                                    : layout.editor_header_bounds.y) +
                               outer_gap;
  const float editor_col_bottom = layout.status_bar_bounds.y - outer_gap;

  const bool has_terminal = m_terminal_panel.is_visible() &&
                            !layout.terminal_panel_bounds.is_empty();

  const float editor_card_bottom =
      has_terminal ? (layout.terminal_panel_bounds.y - sep_gap * 0.5F)
                   : editor_col_bottom;
  geom.editor_card = UI::Rect{
      editor_col_left,
      editor_col_top,
      std::max(0.0F, editor_col_right - editor_col_left),
      std::max(0.0F, editor_card_bottom - editor_col_top),
  };

  if (has_terminal) {
    const float term_top = layout.terminal_panel_bounds.y + sep_gap * 0.5F;
    geom.terminal_card = UI::Rect{
        editor_col_left,
        term_top,
        std::max(0.0F, editor_col_right - editor_col_left),
        std::max(0.0F, editor_col_bottom - term_top),
    };
  }

  const bool has_shader = m_shader_sandbox_panel.is_visible() &&
                          !layout.shader_panel_bounds.is_empty();
  if (has_shader) {
    const float shader_left = shader_split_x + sep_gap * 0.5F;
    geom.shader_card = UI::Rect{
        shader_left,
        layout.shader_panel_bounds.y + outer_gap,
        std::max(0.0F, layout.shader_panel_bounds.right() - outer_gap - shader_left),
        std::max(0.0F, layout.shader_panel_bounds.height - outer_gap * 2.0F),
    };
  }

  return geom;
}

namespace {

void set_card_alpha_channel(uint32_t *pixels, int img_w, int img_h,
                            const UI::Rect &card, float radius) noexcept {
  if (card.is_empty() || !pixels || img_w <= 0 || img_h <= 0)
    return;
  const int x0 = std::clamp(static_cast<int>(std::round(card.x)), 0, img_w);
  const int y0 = std::clamp(static_cast<int>(std::round(card.y)), 0, img_h);
  const int x1 =
      std::clamp(static_cast<int>(std::round(card.right())), 0, img_w);
  const int y1 =
      std::clamp(static_cast<int>(std::round(card.bottom())), 0, img_h);
  const float r = std::min({radius, card.width * 0.5F, card.height * 0.5F});
  const float r2 = r * r;

  for (int y = y0; y < y1; ++y) {
    uint32_t *row = &pixels[y * img_w];
    const float cy = static_cast<float>(y) + 0.5F;
    const bool in_top_corner = (r > 0.0F && cy < card.y + r);
    const bool in_bottom_corner = (r > 0.0F && cy > card.bottom() - r);

    for (int x = x0; x < x1; ++x) {
      const float cx = static_cast<float>(x) + 0.5F;
      const bool in_left_corner = (r > 0.0F && cx < card.x + r);
      const bool in_right_corner = (r > 0.0F && cx > card.right() - r);

      if ((in_top_corner || in_bottom_corner) &&
          (in_left_corner || in_right_corner)) {
        const float corner_cx =
            in_left_corner ? (card.x + r) : (card.right() - r);
        const float corner_cy =
            in_top_corner ? (card.y + r) : (card.bottom() - r);
        const float dx = cx - corner_cx;
        const float dy = cy - corner_cy;
        if (dx * dx + dy * dy > r2) {
          continue; // outside rounded corner -> keep blurred backdrop
        }
      }
      row[x] |= 0xFF000000; // 100% solid opaque
    }
  }
}

} // namespace

void StudioWorkspaceRenderer::apply_solid_card_alpha(
    uint32_t *pixels, int client_width, int client_height,
    float content_top) const noexcept {
  if (!pixels || client_width <= 0 || client_height <= 0)
    return;

  const bool is_modern_style =
      m_palette.is_modern || m_theme.is_modern || m_theme.enable_os_blur;

  if (!is_modern_style) {
    const int top_y = std::clamp(static_cast<int>(std::round(content_top)), 0,
                                 client_height);
    for (int y = top_y; y < client_height; ++y) {
      uint32_t *row = &pixels[y * client_width];
      for (int x = 0; x < client_width; ++x) {
        row[x] |= 0xFF000000;
      }
    }
    return;
  }

  const auto geom =
      get_modern_card_geometry(client_width, client_height, content_top);
  set_card_alpha_channel(pixels, client_width, client_height, geom.sidebar_card,
                         geom.card_radius);
  set_card_alpha_channel(pixels, client_width, client_height, geom.editor_card,
                         geom.card_radius);
  set_card_alpha_channel(pixels, client_width, client_height, geom.terminal_card,
                         geom.card_radius);
  set_card_alpha_channel(pixels, client_width, client_height, geom.shader_card,
                         geom.card_radius);

  if (m_tool_switcher_popup.is_visible()) {
    set_card_alpha_channel(pixels, client_width, client_height,
                           m_tool_switcher_popup.calculate_bounds(m_dpi_scale),
                           8.0F);
  }
}

Graphics::BlurUniforms StudioWorkspaceRenderer::to_blur_uniforms(
    int client_width, int client_height, float content_top) const noexcept {
  Graphics::BlurUniforms uniforms{};
  if (client_width > 0 && client_height > 0) {
    uniforms.texel_width = 1.0F / static_cast<float>(client_width);
    uniforms.texel_height = 1.0F / static_cast<float>(client_height);
  }

  const auto geom =
      get_modern_card_geometry(client_width, client_height, content_top);

  uniforms.explorer_card = {geom.sidebar_card.x, geom.sidebar_card.y,
                           geom.sidebar_card.width, geom.sidebar_card.height};
  uniforms.editor_card = {geom.editor_card.x, geom.editor_card.y,
                         geom.editor_card.width, geom.editor_card.height};
  uniforms.terminal_card = {geom.terminal_card.x, geom.terminal_card.y,
                           geom.terminal_card.width, geom.terminal_card.height};
  uniforms.shader_card = {geom.shader_card.x, geom.shader_card.y,
                         geom.shader_card.width, geom.shader_card.height};
  if (m_tool_switcher_popup.is_visible()) {
    const auto pb = m_tool_switcher_popup.calculate_bounds(m_dpi_scale);
    uniforms.popup_card = {pb.x, pb.y, pb.width, pb.height};
  }
  uniforms.card_radius = geom.card_radius;
  uniforms.enable_blur = (m_theme.enable_os_blur || m_theme.is_modern) ? 1 : 0;
  return uniforms;
}

void StudioWorkspaceRenderer::render(Drawable drawable, int client_width,
                                     int client_height,
                                     float content_top) const {
  if (m_display == nullptr || m_graphics_context == nullptr || drawable == 0 ||
      m_ui_font == nullptr || m_small_font == nullptr ||
      m_editor_font == nullptr || m_minimap_font == nullptr ||
      m_large_font == nullptr) {
    return;
  }
  auto &pm = Zenvra::Plugins::PluginManager::instance();
  auto active_tool = pm.get_active_tool_plugin();
  if (active_tool) {
    UI::Editor::set_active_tool_sidebar_item(active_tool->get_id(),
                                             active_tool->get_name());
  } else {
    UI::Editor::clear_active_tool_sidebar_item();
  }

  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);

  const bool is_modern_style =
      m_palette.is_modern || m_theme.is_modern || m_theme.enable_os_blur;

  if (!is_modern_style) {
    fill_rectangle(drawable, layout.workspace_bounds,
                   m_pixels.workspace_background);
    fill_rectangle(drawable, layout.activity_bar_bounds,
                   m_pixels.sidebar_background);

    const bool tabs_are_in_titlebar =
        layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
    if (tabs_are_in_titlebar) {
      m_text_editor.draw_tab_strip(*this, drawable, layout);
    } else {
      fill_rectangle(drawable, layout.tab_bar_bounds,
                     m_pixels.tab_background);
      m_text_editor.draw_tab_strip(*this, drawable, layout);
    }
    fill_rectangle(drawable, layout.tool_sidebar_bounds,
                   m_pixels.sidebar_background);
    fill_rectangle(drawable, layout.editor_header_bounds,
                   m_pixels.editor_background);
    fill_rectangle(drawable, layout.gutter_bounds, m_pixels.editor_background);
    fill_rectangle(drawable, layout.editor_bounds, m_pixels.editor_background);
    fill_rectangle(drawable, layout.status_bar_bounds,
                   m_pixels.status_background);

    m_activity_sidebar.render(*this, drawable, layout);
    m_tool_sidebar.render(*this, drawable, layout);
    m_text_editor.render(*this, drawable, layout);
    m_terminal_panel.render(*this, drawable, layout);
    m_shader_sandbox_panel.render(*this, drawable, layout);
  } else {
    // Modern mode: unified seamless window surface (solid or blurred)
    fill_rectangle(drawable, layout.workspace_bounds,
                   m_pixels.workspace_background);

    // Activity bar icons rendered directly on unified background
    m_activity_sidebar.render(*this, drawable, layout);

    // Tabs float directly on the unified titlebar canvas
    m_text_editor.draw_tab_strip(*this, drawable, layout);

    const auto geom =
        get_modern_card_geometry(client_width, client_height, content_top);
    const float card_radius = geom.card_radius;

    // Sidebar card wrapper
    if (!geom.sidebar_card.is_empty()) {
      fill_rounded_rectangle(drawable, geom.sidebar_card,
                             m_pixels.sidebar_background, card_radius,
                             m_pixels.workspace_background);
      push_clip(geom.sidebar_card);
      m_tool_sidebar.render(*this, drawable, layout);
      pop_clip();
      draw_rounded_rectangle(drawable, geom.sidebar_card, m_pixels.border,
                             card_radius);
    }

    // Text editor card wrapper
    if (!geom.editor_card.is_empty()) {
      fill_rounded_rectangle(drawable, geom.editor_card,
                             m_pixels.editor_background, card_radius,
                             m_pixels.workspace_background);
      push_clip(geom.editor_card);
      m_text_editor.render(*this, drawable, layout);
      pop_clip();
      draw_rounded_rectangle(drawable, geom.editor_card, m_pixels.border,
                             card_radius);
    }

    // Terminal panel card wrapper
    if (!geom.terminal_card.is_empty()) {
      fill_rounded_rectangle(drawable, geom.terminal_card,
                             m_pixels.editor_background, card_radius,
                             m_pixels.workspace_background);
      push_clip(geom.terminal_card);
      m_terminal_panel.render(*this, drawable, layout);
      pop_clip();
      draw_rounded_rectangle(drawable, geom.terminal_card, m_pixels.border,
                             card_radius);
    }

    // Shader sandbox card wrapper
    if (!geom.shader_card.is_empty()) {
      fill_rounded_rectangle(drawable, geom.shader_card,
                             m_pixels.sidebar_background, card_radius,
                             m_pixels.workspace_background);
      push_clip(geom.shader_card);
      m_shader_sandbox_panel.render(*this, drawable, layout);
      pop_clip();
      draw_rounded_rectangle(drawable, geom.shader_card, m_pixels.border,
                             card_radius);
    }
  }

  if (m_tool_switcher_popup.is_visible()) {
    m_tool_switcher_popup.render(*this, drawable, m_dpi_scale);
  }
  if (m_settings_window.is_visible()) {
    render_settings_window(drawable, client_width, client_height);
  }
  if (const UI::Editor::TextDocumentModel *document =
          m_text_editor.get_document()) {
    m_footer_toolbar.render(*this, drawable, layout,
                            document->get_full_breadcrumbs(),
                            document->get_status());
  } else {
    m_footer_toolbar.render(*this, drawable, layout, {},
                            UI::Editor::FooterEditorStatus{});
  }

  // Floating overlays (e.g. completions, diagnostics, menus) rendered on top of cards
  m_text_editor.render_overlays(*this, drawable, layout);
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

void StudioWorkspaceRenderer::render_prompt_modal(Drawable drawable,
                                                  int client_width,
                                                  int client_height) const {
  if (!m_prompt_modal.is_visible()) {
    return;
  }

  const UI::Rect viewport{0.0F, 0.0F, static_cast<float>(client_width),
                          static_cast<float>(client_height)};
  const auto layout = m_prompt_modal.calculate_layout(viewport, m_dpi_scale);

  // 1. Semi-transparent backdrop overlay
  fill_rectangle(drawable, layout.base_layout.backdrop_bounds,
                 allocate_color(UI::Theme::Color{0, 0, 0, 140}));

  // 2. Dialog Container (VS Code sleek dark card)
  const UI::Theme::Color dialog_bg{30, 30, 34, 255};
  const UI::Theme::Color border_col{60, 64, 75, 255};

  fill_rounded_rectangle(drawable, layout.base_layout.dialog_bounds,
                         allocate_color(dialog_bg), 6.0F * m_dpi_scale,
                         m_pixels.workspace_background);
  draw_rectangle(drawable, layout.base_layout.dialog_bounds,
                 allocate_color(border_col));

  // 3. Title & Subtitle
  draw_text(drawable, *m_ui_font, m_prompt_modal.get_title(),
            layout.title_bounds.x,
            layout.title_bounds.y + layout.title_bounds.height * 0.5F,
            m_text.primary);
  draw_text(drawable, *m_small_font, m_prompt_modal.get_subtitle(),
            layout.subtitle_bounds.x,
            layout.subtitle_bounds.y + layout.subtitle_bounds.height * 0.5F,
            m_text.muted);

  // 4. Close (X) button
  const auto close_bg = m_prompt_modal.is_close_hovered()
                            ? UI::Theme::Color{232, 17, 35, 255}
                            : dialog_bg;
  if (m_prompt_modal.is_close_hovered()) {
    fill_rounded_rectangle(drawable, layout.close_button_bounds,
                           allocate_color(close_bg), 3.0F * m_dpi_scale,
                           allocate_color(dialog_bg));
  }
  draw_text(
      drawable, *m_ui_font, "x",
      layout.close_button_bounds.x + layout.close_button_bounds.width * 0.3F,
      layout.close_button_bounds.y + layout.close_button_bounds.height * 0.5F,
      m_text.muted);

  // 5. Input field (if not ConfirmDelete)
  if (m_prompt_modal.get_mode() != UI::Components::PromptMode::ConfirmDelete) {
    const auto &input = m_prompt_modal.get_input();
    const UI::Theme::Color input_bg{20, 20, 24, 255};
    const UI::Theme::Color input_border = input.get_state().focused
                                              ? UI::Theme::Color{0, 122, 204, 255}
                                              : border_col;
    fill_rounded_rectangle(drawable, layout.input_bounds,
                           allocate_color(input_bg), 3.0F * m_dpi_scale,
                           allocate_color(dialog_bg));
    draw_rectangle(drawable, layout.input_bounds, allocate_color(input_border));

    const std::string &text = input.get_text();
    const float text_x = layout.input_bounds.x + 8.0F * m_dpi_scale;
    const float text_y =
        layout.input_bounds.y + layout.input_bounds.height * 0.5F;

    if (text.empty()) {
      draw_text(drawable, *m_ui_font, input.get_placeholder(), text_x, text_y,
                m_text.muted);
    } else {
      draw_text(drawable, *m_editor_font, text, text_x, text_y, m_text.primary);
    }

    // Draw Caret if input is focused and blinking visible
    if (input.is_caret_visible()) {
      const std::string prefix{input.get_text_before_cursor()};
      const int text_w = m_editor_font->getTextWidth(prefix);
      const float caret_x = text_x + static_cast<float>(text_w);
      draw_line(drawable, round_to_int(caret_x),
                round_to_int(layout.input_bounds.y + 5.0F * m_dpi_scale),
                round_to_int(caret_x),
                round_to_int(layout.input_bounds.bottom() - 5.0F * m_dpi_scale),
                m_pixels.text_primary);
    }
  }

  // 6. Cancel Button
  const auto cancel_bg = m_prompt_modal.is_cancel_hovered()
                             ? UI::Theme::Color{55, 55, 62, 255}
                             : UI::Theme::Color{45, 45, 50, 255};
  fill_rounded_rectangle(drawable, layout.cancel_button_bounds,
                         allocate_color(cancel_bg), 3.0F * m_dpi_scale,
                         allocate_color(dialog_bg));
  draw_rectangle(drawable, layout.cancel_button_bounds,
                 allocate_color(border_col));
  draw_text(drawable, *m_ui_font, "Cancel",
            layout.cancel_button_bounds.x + 22.0F * m_dpi_scale,
            layout.cancel_button_bounds.y +
                layout.cancel_button_bounds.height * 0.5F,
            m_text.primary);

  // 7. OK / Confirm Button
  UI::Theme::Color ok_bg =
      (m_prompt_modal.get_mode() == UI::Components::PromptMode::ConfirmDelete)
          ? (m_prompt_modal.is_ok_hovered()
                 ? UI::Theme::Color{232, 17, 35, 255}
                 : UI::Theme::Color{180, 20, 30, 255})
          : (m_prompt_modal.is_ok_hovered()
                 ? UI::Theme::Color{0, 122, 204, 255}
                 : UI::Theme::Color{14, 99, 156, 255});
  fill_rounded_rectangle(drawable, layout.ok_button_bounds,
                         allocate_color(ok_bg), 3.0F * m_dpi_scale,
                         allocate_color(dialog_bg));
  draw_text(drawable, *m_ui_font, m_prompt_modal.get_confirm_label(),
            layout.ok_button_bounds.x + 24.0F * m_dpi_scale,
            layout.ok_button_bounds.y + layout.ok_button_bounds.height * 0.5F,
            m_text.primary);
}

bool StudioWorkspaceRenderer::is_settings_window_visible() const noexcept {
  return m_settings_window.is_visible();
}

void StudioWorkspaceRenderer::render_settings_window(
    Drawable drawable, int client_width, int client_height) const {
  if (!m_settings_window.is_visible()) {
    return;
  }
  const auto layout = m_settings_window.calculate_layout(
      static_cast<float>(client_width), static_cast<float>(client_height),
      m_dpi_scale);
  const float scale = m_dpi_scale;
  const auto& theme = m_settings_window.get_theme();

  auto centered_x = [&](AntialiasedFont& font, const UI::Rect& rc,
                        std::string_view text) -> float {
    const int tw = get_text_width(font, text);
    return rc.x + (rc.width - static_cast<float>(tw)) * 0.5F;
  };

  // 1. Dim backdrop + dialog card + border (Win32 §1, §9).
  fill_rectangle(drawable, layout.overlay_bounds,
                 allocate_color(UI::Theme::Color{0, 0, 0, 140}));
  const UI::Theme::Color dialog_bg = theme.panel_background;
  const UI::Theme::Color border_col = theme.titlebar_border;
  const unsigned long dialog_px = allocate_color(dialog_bg);
  const unsigned long border_px = allocate_color(border_col);
  fill_rounded_rectangle(drawable, layout.dialog_bounds, dialog_px,
                         6.0F * scale, m_pixels.workspace_background);
  draw_rounded_rectangle(drawable, layout.dialog_bounds, border_px,
                         6.0F * scale);

  // 2. Header: gear icon + "Settings" + close X (Win32 §2).
  draw_svg_icon(drawable, "Assets/icons/gear.svg",
                round_to_int(layout.header_bounds.x + 14.0F * scale + 7.5F * scale),
                round_to_int(layout.header_bounds.y + layout.header_bounds.height * 0.5F),
                std::max(round_to_int(15.0F * scale), 12),
                theme.text_secondary, dialog_bg, false);
  if (m_ui_font) {
    draw_text(drawable, *m_ui_font, "Settings", layout.title_bounds.x,
              layout.title_bounds.y + layout.title_bounds.height * 0.5F,
              m_text.primary);
  }
  if (layout.close_hovered) {
    fill_rectangle(drawable, layout.close_btn_bounds,
                   allocate_color(UI::Theme::Color{232, 17, 35, 255}));
  }
  {
    const float cx = layout.close_btn_bounds.x + layout.close_btn_bounds.width * 0.5F;
    const float cy = layout.close_btn_bounds.y + layout.close_btn_bounds.height * 0.5F;
    const float h = 5.0F * scale;
    const unsigned long col = allocate_color(
        layout.close_hovered ? UI::Theme::Color{255, 255, 255, 255}
                             : theme.text_secondary);
    draw_line(drawable, round_to_int(cx - h), round_to_int(cy - h),
              round_to_int(cx + h), round_to_int(cy + h), col);
    draw_line(drawable, round_to_int(cx - h), round_to_int(cy + h),
              round_to_int(cx + h), round_to_int(cy - h), col);
  }

  // 3. Search bar (Win32 §3).
  if (!layout.search_bar_bounds.is_empty()) {
    const bool search_focused = !m_settings_window.get_search_query().empty();
    fill_rounded_rectangle(
        drawable, layout.search_bar_bounds,
        allocate_color(theme.command_center_background), 3.0F * scale, dialog_px);
    draw_rounded_rectangle(
        drawable, layout.search_bar_bounds,
        allocate_color(search_focused ? theme.accent : theme.command_center_border),
        3.0F * scale);
    draw_svg_icon(drawable, "Assets/icons/search.svg",
                  round_to_int(layout.search_bar_bounds.x + 10.0F * scale + 7.0F * scale),
                  round_to_int(layout.search_bar_bounds.y + layout.search_bar_bounds.height * 0.5F),
                  std::max(round_to_int(14.0F * scale), 11),
                  theme.text_secondary, theme.command_center_background, false);
    if (m_ui_font) {
      const std::string query = m_settings_window.get_search_query();
      draw_text(drawable, *m_ui_font,
                query.empty() ? "Search settings" : query,
                layout.search_bar_bounds.x + 32.0F * scale,
                layout.search_bar_bounds.y + layout.search_bar_bounds.height * 0.5F,
                query.empty() ? m_text.muted : m_text.primary);
    }
    if (!layout.search_clear_btn_bounds.is_empty() && m_small_font) {
      draw_text(drawable, *m_small_font, "x",
                layout.search_clear_btn_bounds.x + 4.0F * scale,
                layout.search_clear_btn_bounds.y + layout.search_clear_btn_bounds.height * 0.5F,
                m_text.muted);
    }
  }

  // 4. Scope tabs User | Workspace + underline (Win32 §4).
  if (m_ui_font) {
    const bool user_active =
        m_settings_window.get_active_scope() == Zenvra::Settings::SettingsScope::User;
    draw_text(drawable, *m_ui_font, "User", layout.user_tab_bounds.x,
              layout.user_tab_bounds.y + layout.user_tab_bounds.height * 0.5F,
              user_active ? m_text.primary : m_text.muted);
    draw_text(drawable, *m_ui_font, "Workspace", layout.workspace_tab_bounds.x,
              layout.workspace_tab_bounds.y + layout.workspace_tab_bounds.height * 0.5F,
              !user_active ? m_text.primary : m_text.muted);
  }
  // 4.5 Borders: header separator + active underline + sidebar divider (Win32 §4.5).
  if (!layout.header_separator_bounds.is_empty()) {
    draw_line(drawable, round_to_int(layout.dialog_bounds.x),
              round_to_int(layout.header_separator_bounds.y),
              round_to_int(layout.dialog_bounds.right()),
              round_to_int(layout.header_separator_bounds.y), border_px);
  }
  if (!layout.tab_underline_bounds.is_empty()) {
    fill_rectangle(drawable, layout.tab_underline_bounds,
                   allocate_color(theme.accent));
  }
  if (!layout.sidebar_divider_bounds.is_empty()) {
    draw_line(drawable, round_to_int(layout.sidebar_divider_bounds.x),
              round_to_int(layout.sidebar_divider_bounds.y),
              round_to_int(layout.sidebar_divider_bounds.x),
              round_to_int(layout.dialog_bounds.bottom()), border_px);
  }

  // 5. Sidebar category tree with clip (Win32 §5).
  push_clip(layout.sidebar_bounds);
  for (const auto& item : layout.category_items) {
    if (item.bounds.bottom() < layout.sidebar_bounds.y ||
        item.bounds.y > layout.sidebar_bounds.bottom()) {
      continue;
    }
    const bool is_active =
        (item.id == m_settings_window.get_active_category() ||
         item.name == m_settings_window.get_active_category()) &&
        m_settings_window.get_search_query().empty();
    const bool is_hovered = (item.id == layout.hovered_category ||
                             item.name == layout.hovered_category);
    if (is_active) {
      draw_rounded_rectangle(drawable, item.bounds, allocate_color(theme.accent),
                             2.0F * scale);
    } else if (is_hovered) {
      fill_rounded_rectangle(drawable, item.bounds, m_pixels.hover_background,
                             2.0F * scale, dialog_px);
    }
    if (item.has_children) {
      const int ch_sz = std::max(round_to_int(10.0F * scale), 8);
      draw_svg_icon(drawable,
                    item.is_expanded ? "Assets/icons/chevron-down.svg"
                                     : "Assets/icons/chevron-right.svg",
                    round_to_int(item.bounds.x + 6.0F * scale + ch_sz * 0.5F),
                    round_to_int(item.bounds.y + item.bounds.height * 0.5F),
                    ch_sz, theme.text_secondary, dialog_bg, false);
    }
    if (m_small_font) {
      const float indent = item.has_children
                               ? (item.bounds.x + 22.0F * scale)
                               : (item.depth == 0 ? (item.bounds.x + 22.0F * scale)
                                                  : (item.bounds.x + 36.0F * scale));
      draw_text(drawable, *m_small_font, item.name, indent,
                item.bounds.y + item.bounds.height * 0.5F,
                is_active ? m_text.primary : m_text.muted);
    }
  }
  pop_clip();

  // Sidebar scrollbar track & thumb (Win32 §5 tail).
  if (!layout.sidebar_scrollbar_track.is_empty()) {
    fill_rounded_rectangle(
        drawable, layout.sidebar_scrollbar_track,
        allocate_color(UI::Theme::Color{18, 19, 22, 160}),
        2.0F * scale, dialog_px);
  }
  if (!layout.sidebar_scrollbar_thumb.is_empty()) {
    const bool hot = layout.sidebar_scrollbar_thumb_hovered ||
                     m_settings_window.is_dragging_sidebar_scrollbar();
    fill_rounded_rectangle(
        drawable, layout.sidebar_scrollbar_thumb,
        allocate_color(hot ? UI::Theme::Color{95, 100, 110, 255} : (theme.is_dark ? UI::Theme::Color{65, 70, 80, 255} : theme.hover)),
        2.0F * scale, dialog_px);
  }

  // 6. Content area with clip: section headers + rows (Win32 §6).
  auto& service = Zenvra::Settings::SettingsService::instance();
  push_clip(layout.content_bounds);
  for (const auto& hdr : layout.section_headers) {
    if (hdr.bounds.bottom() < layout.content_bounds.y ||
        hdr.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }
    if (m_large_font) {
      draw_text(drawable, *m_large_font, hdr.title, hdr.bounds.x,
                hdr.bounds.y + hdr.bounds.height * 0.5F, m_text.primary);
    }
  }
  for (const auto& row : layout.rows) {
    if (!row.def) continue;
    if (row.bounds.bottom() < layout.content_bounds.y ||
        row.bounds.y > layout.content_bounds.bottom()) {
      continue;
    }
    const auto* def = row.def;

    // Focus box + gear (Win32 row header).
    if (row.is_focused) {
      draw_rounded_rectangle(drawable, row.focus_box_bounds,
                             allocate_color(theme.accent), 2.0F * scale);
      draw_svg_icon(drawable, "Assets/icons/gear.svg",
                    round_to_int(row.gear_btn_bounds.x + row.gear_btn_bounds.width * 0.5F),
                    round_to_int(row.gear_btn_bounds.y + row.gear_btn_bounds.height * 0.5F),
                    std::max(round_to_int(15.0F * scale), 12),
                    theme.text_secondary, dialog_bg, false);
    }

    // Title: "Category: Title" (+ "(Modified)").
    if (m_ui_font) {
      const std::string title = def->category + ": " + def->title +
                                (row.is_modified ? "  (Modified)" : "");
      draw_text(drawable, *m_ui_font, title, row.label_bounds.x,
                row.label_bounds.y + row.label_bounds.height * 0.5F,
                m_text.primary);
    }
    if (m_small_font && !row.description_bounds.is_empty()) {
      draw_text(drawable, *m_small_font, def->description,
                row.description_bounds.x,
                row.description_bounds.y + row.description_bounds.height * 0.5F,
                m_text.muted);
    }

    // Controls per type.
    if (def->type == Zenvra::Settings::SettingType::Boolean) {
      const bool val = service.get<bool>(def->id);
      const UI::Theme::Color bg = val ? theme.accent
          : (row.is_checkbox_hovered ? theme.command_center_background
                                     : UI::Theme::Color{24, 25, 28, 255});
      const UI::Theme::Color bd = (val || row.is_checkbox_hovered)
                                      ? theme.accent
                                      : theme.command_center_border;
      fill_rounded_rectangle(drawable, row.checkbox_bounds, allocate_color(bg),
                             2.0F * scale, dialog_px);
      draw_rounded_rectangle(drawable, row.checkbox_bounds, allocate_color(bd),
                             2.0F * scale);
      if (val) {
        const unsigned long white = allocate_color(UI::Theme::Color{255, 255, 255, 255});
        const int cx = round_to_int(row.checkbox_bounds.x);
        const int cy = round_to_int(row.checkbox_bounds.y);
        const int s = std::max(round_to_int(scale), 1);
        draw_line(drawable, cx + 4 * s, cy + 9 * s, cx + 7 * s, cy + 13 * s, white);
        draw_line(drawable, cx + 7 * s, cy + 13 * s, cx + 14 * s, cy + 5 * s, white);
      }
    } else if (def->id == "editor.fontFamily") {
      const bool is_open = m_settings_window.get_dropdown().is_open() &&
                           (m_settings_window.get_dropdown().get_owner_id() == "editor.fontFamily");
      fill_rounded_rectangle(
          drawable, row.input_bounds,
          allocate_color(theme.command_center_background), 2.5F * scale, dialog_px);
      draw_rounded_rectangle(
          drawable, row.input_bounds,
          allocate_color(is_open ? theme.accent
                         : (row.is_input_hovered ? theme.hover : theme.command_center_border)),
          2.5F * scale);
      if (m_ui_font) {
        const std::string cur = service.get<std::string>(def->id);
        draw_text(drawable, *m_ui_font, cur,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  m_text.primary);
      }
      draw_svg_icon(drawable, "Assets/icons/chevron-down.svg",
                    round_to_int(row.dropdown_btn_bounds.x + row.dropdown_btn_bounds.width * 0.5F),
                    round_to_int(row.dropdown_btn_bounds.y + row.dropdown_btn_bounds.height * 0.5F),
                    std::max(round_to_int(9.0F * scale), 8),
                    theme.text_secondary, theme.command_center_background, false);
    } else if (def->id == "workbench.mascot.image") {
      const std::string img_path = service.get<std::string>(def->id);
      const UI::Theme::Color thumb_bg{20, 21, 24, 255};
      fill_rounded_rectangle(drawable, row.thumbnail_bounds, allocate_color(thumb_bg),
                             3.0F * scale, dialog_px);
      draw_rounded_rectangle(
          drawable, row.thumbnail_bounds,
          allocate_color((row.is_thumbnail_hovered || row.is_focused)
                             ? theme.accent
                             : theme.command_center_border),
          3.0F * scale);
      // Thumbnail placeholder: draw icon + filename (full bitmap blit is
      // Win32-GDI only; X11 shows asset-slot style box).
      draw_svg_icon(drawable, "Assets/icons/image.svg",
                    round_to_int(row.thumbnail_bounds.x + row.thumbnail_bounds.width * 0.5F),
                    round_to_int(row.thumbnail_bounds.y + row.thumbnail_bounds.height * 0.5F - 8.0F * scale),
                    std::max(round_to_int(22.0F * scale), 16),
                    theme.text_secondary, thumb_bg, false);
      if (m_small_font) {
        std::string mode;
        try { mode = service.get<std::string>("workbench.mascot.renderMode"); }
        catch (...) { mode = "default"; }
        std::string ext = (mode == "ascii") ? "ASCII" : "";
        if (ext.empty()) {
          const auto dot = img_path.find_last_of('.');
          ext = (dot == std::string::npos) ? "PNG" : img_path.substr(dot + 1);
          for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
          if (ext.empty()) ext = "PNG";
        }
        const UI::Rect badge{row.thumbnail_bounds.x + 3.0F * scale,
                             row.thumbnail_bounds.bottom() - 13.0F * scale - 3.0F * scale,
                             (ext == "ASCII" ? 38.0F : 28.0F) * scale, 13.0F * scale};
        fill_rounded_rectangle(drawable, badge,
                               allocate_color(UI::Theme::Color{12, 14, 18, 255}),
                               2.0F * scale, allocate_color(thumb_bg));
        draw_text(drawable, *m_small_font, ext,
                  badge.x + 4.0F * scale, badge.y + badge.height * 0.5F,
                  m_text.muted);
        std::string disp = img_path.empty() ? "zenvra_logo.png (Default Mascot)" : img_path;
        draw_text(drawable, *m_small_font, disp,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  m_text.primary);
      }
      auto button = [&](const UI::Rect& rc, std::string_view label, bool hovered,
                        bool enabled = true) {
        fill_rounded_rectangle(
            drawable, rc,
            allocate_color(hovered && enabled ? theme.hover : theme.command_center_background),
            2.5F * scale, dialog_px);
        draw_rounded_rectangle(
            drawable, rc,
            allocate_color(hovered && enabled ? theme.accent : theme.command_center_border),
            2.5F * scale);
        if (m_ui_font) {
          draw_text(drawable, *m_ui_font, label, centered_x(*m_ui_font, rc, label),
                    rc.y + rc.height * 0.5F,
                    enabled ? m_text.primary : m_text.muted);
        }
      };
      button(row.browse_btn_bounds, "Browse...", row.is_browse_hovered);
      button(row.reset_btn_bounds, "Reset", row.is_reset_hovered, row.is_modified);
      if (m_small_font) {
        draw_text(drawable, *m_small_font, "Drop image here (.png, .jpg, .bmp)",
                  row.reset_btn_bounds.right() + 12.0F * scale,
                  row.browse_btn_bounds.y + row.browse_btn_bounds.height * 0.5F,
                  m_text.muted);
      }
    } else if (def->id == "workbench.mascot.renderMode") {
      const bool is_ascii =
          (service.get<std::string>("workbench.mascot.renderMode") == "ascii");
      auto switch_btn = [&](const UI::Rect& rc, std::string_view label, bool active,
                            bool hovered) {
        const UI::Theme::Color bg =
            active ? theme.accent : (hovered ? theme.hover : theme.command_center_background);
        const UI::Theme::Color bd =
            active ? theme.accent : (hovered ? theme.accent : theme.command_center_border);
        fill_rounded_rectangle(drawable, rc, allocate_color(bg), 3.0F * scale, dialog_px);
        draw_rounded_rectangle(drawable, rc, allocate_color(bd), 3.0F * scale);
        if (m_ui_font) {
          if (active) {
            draw_text(drawable, *m_ui_font, label, centered_x(*m_ui_font, rc, label),
                      rc.y + rc.height * 0.5F, "#ffffff");
          } else {
            draw_text(drawable, *m_ui_font, label, centered_x(*m_ui_font, rc, label),
                      rc.y + rc.height * 0.5F, m_text.muted);
          }
        }
      };
      switch_btn(row.switch_default_btn_bounds, "Default (Solid)", !is_ascii,
                 row.is_switch_default_hovered);
      switch_btn(row.switch_ascii_btn_bounds, "ASCII Art", is_ascii,
                 row.is_switch_ascii_hovered);
    } else if ((def->type == Zenvra::Settings::SettingType::Integer ||
                def->type == Zenvra::Settings::SettingType::Float ||
                def->type == Zenvra::Settings::SettingType::String) &&
               def->id != "workbench.mascot.image") {
      const bool is_editing =
          (m_settings_window.get_editing_setting_id() == def->id);
      fill_rounded_rectangle(drawable, row.input_bounds,
                             allocate_color(theme.command_center_background),
                             2.5F * scale, dialog_px);
      draw_rounded_rectangle(
          drawable, row.input_bounds,
          allocate_color(is_editing ? theme.accent
                         : (row.is_input_hovered ? theme.hover : theme.command_center_border)),
          2.5F * scale);
      if (m_ui_font) {
        const std::string val = is_editing ? m_settings_window.get_editing_text()
                                           : service.get(def->id).to_display_string();
        draw_text(drawable, *m_ui_font, val,
                  row.input_bounds.x + 8.0F * scale,
                  row.input_bounds.y + row.input_bounds.height * 0.5F,
                  m_text.primary);
        if (is_editing && m_settings_window.is_caret_visible()) {
          const int tw = get_text_width(*m_ui_font, val);
          const int cx = round_to_int(row.input_bounds.x + 8.0F * scale + tw + 1);
          const int top = round_to_int(row.input_bounds.y +
                                       (row.input_bounds.height - 16.0F * scale) * 0.5F);
          const int bot = top + round_to_int(16.0F * scale);
          draw_line(drawable, cx, top, cx, bot, allocate_color(theme.text_primary));
        }
      }
    } else if (def->type == Zenvra::Settings::SettingType::Enum &&
               def->id != "workbench.mascot.renderMode") {
      const bool is_open = m_settings_window.get_dropdown().is_open() &&
                           (m_settings_window.get_dropdown().get_owner_id() == def->id);
      fill_rounded_rectangle(
          drawable, row.option_btn_bounds,
          allocate_color((row.is_option_hovered || is_open) ? theme.hover
                                                            : theme.command_center_background),
          2.5F * scale, dialog_px);
      draw_rounded_rectangle(
          drawable, row.option_btn_bounds,
          allocate_color((row.is_option_hovered || is_open) ? theme.accent
                                                            : theme.command_center_border),
          2.5F * scale);
      if (m_ui_font) {
        const std::string cur = service.get<std::string>(def->id);
        draw_text(drawable, *m_ui_font, cur,
                  row.option_btn_bounds.x + 10.0F * scale,
                  row.option_btn_bounds.y + row.option_btn_bounds.height * 0.5F,
                  m_text.primary);
      }
      draw_svg_icon(drawable, "Assets/icons/chevron-down.svg",
                    round_to_int(row.option_btn_bounds.right() - 16.0F * scale),
                    round_to_int(row.option_btn_bounds.y + row.option_btn_bounds.height * 0.5F),
                    std::max(round_to_int(9.0F * scale), 8),
                    theme.text_secondary, theme.command_center_background, false);
    }
  }
  pop_clip();

  // 7. Content scrollbar (Win32 §7).
  if (!layout.scrollbar_track.is_empty()) {
    fill_rounded_rectangle(
        drawable, layout.scrollbar_track,
        allocate_color(UI::Theme::Color{18, 19, 22, 160}),
        3.0F * scale, dialog_px);
  }
  if (!layout.scrollbar_thumb.is_empty()) {
    const bool hot = layout.scrollbar_thumb_hovered ||
                     m_settings_window.is_dragging_scrollbar();
    fill_rounded_rectangle(
        drawable, layout.scrollbar_thumb,
        allocate_color(hot ? UI::Theme::Color{100, 106, 120, 255} : (theme.is_dark ? UI::Theme::Color{65, 70, 80, 255} : theme.hover)),
        3.0F * scale, dialog_px);
  }

  // 8. Dropdown popover from cross-platform layout data (Win32 §8).
  {
    const auto& dd = m_settings_window.get_dropdown();
    if (dd.is_open() && m_small_font && m_ui_font) {
      const UI::Rect& b = dd.get_bounds();
      if (!b.is_empty()) {
        const unsigned long dd_bg = allocate_color(theme.panel_background);
        const unsigned long dd_border = allocate_color(theme.titlebar_border);
        fill_rounded_rectangle(drawable, b, dd_bg, 6.0F * scale, dialog_px);
        draw_rounded_rectangle(drawable, b, dd_border, 6.0F * scale);
        push_clip(b);
        for (const auto& it : dd.get_layout_items()) {
          if (it.bounds.is_empty()) continue;
          if (it.is_hovered) {
            fill_rounded_rectangle(drawable, it.bounds, m_pixels.hover_background,
                                   3.0F * scale, dd_bg);
          }
          float text_x = it.bounds.x + 10.0F * scale;
          if (!it.item.preview_colors.empty()) {
            float swatch_x = text_x;
            const float swatch_w = 14.0F * scale;
            const float swatch_h = 14.0F * scale;
            const float swatch_y = it.bounds.y + (it.bounds.height - swatch_h) * 0.5F;
            for (const auto& color : it.item.preview_colors) {
              fill_rounded_rectangle(drawable, UI::Rect{swatch_x, swatch_y, swatch_w, swatch_h},
                                     allocate_color(color), 2.0F * scale, dd_bg);
              swatch_x += swatch_w + 3.0F * scale;
            }
            text_x = swatch_x + 6.0F * scale;
          }
          draw_text(drawable, *m_small_font, it.item.label,
                    text_x,
                    it.bounds.y + it.bounds.height * 0.5F,
                    it.is_selected ? m_text.primary : m_text.muted);
          if (!it.item.description.empty()) {
            const int desc_w = get_text_width(*m_small_font, it.item.description);
            draw_text(drawable, *m_small_font, it.item.description,
                      it.bounds.right() - static_cast<float>(desc_w) - 10.0F * scale,
                      it.bounds.y + it.bounds.height * 0.5F,
                      m_text_dimmed.muted);
          }
        }
        pop_clip();

        const auto& dd_track = dd.get_scrollbar_track();
        const auto& dd_thumb = dd.get_scrollbar_thumb();
        if (!dd_track.is_empty() && !dd_thumb.is_empty()) {
          fill_rounded_rectangle(drawable, dd_track,
                                 allocate_color(UI::Theme::Color{15, 16, 18, 160}),
                                 2.0F * scale, dd_bg);
          const bool dd_hot = dd.is_scrollbar_thumb_hovered() || dd.is_dragging_scrollbar();
          fill_rounded_rectangle(drawable, dd_thumb,
                                 allocate_color(dd_hot ? UI::Theme::Color{110, 115, 130, 255} : UI::Theme::Color{70, 75, 85, 255}),
                                 2.0F * scale, dd_bg);
        }
      }
    }
  }
}

const std::filesystem::path &
StudioWorkspaceRenderer::get_icon_asset_root() const noexcept {
  return m_icon_asset_root;
}

unsigned long
StudioWorkspaceRenderer::allocate_color(const UI::Theme::Color &color) const {
  XColor x_color{};
  x_color.red = static_cast<unsigned short>(color.red * 257U);
  x_color.green = static_cast<unsigned short>(color.green * 257U);
  x_color.blue = static_cast<unsigned short>(color.blue * 257U);
  x_color.flags = DoRed | DoGreen | DoBlue;
  if (XAllocColor(m_display, DefaultColormap(m_display, m_screen), &x_color) ==
      0) {
    return BlackPixel(m_display, m_screen);
  }
  return x_color.pixel;
}

void StudioWorkspaceRenderer::push_clip(const UI::Rect &rect) const {
  UI::Rect eff = rect;
  if (!m_clip_stack.empty()) {
    const auto &top = m_clip_stack.back();
    const float x1 = std::max(top.x, rect.x);
    const float y1 = std::max(top.y, rect.y);
    const float x2 = std::min(top.right(), rect.right());
    const float y2 = std::min(top.bottom(), rect.bottom());
    eff = UI::Rect{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
  }
  m_clip_stack.push_back(eff);

  XRectangle xrect;
  xrect.x = static_cast<short>(round_to_int(eff.x));
  xrect.y = static_cast<short>(round_to_int(eff.y));
  xrect.width =
      static_cast<unsigned short>(std::max(round_to_int(eff.width), 0));
  xrect.height =
      static_cast<unsigned short>(std::max(round_to_int(eff.height), 0));
  XSetClipRectangles(m_display, m_graphics_context, 0, 0, &xrect, 1, Unsorted);
}

void StudioWorkspaceRenderer::pop_clip() const {
  if (!m_clip_stack.empty()) {
    m_clip_stack.pop_back();
  }
  if (m_clip_stack.empty()) {
    XSetClipMask(m_display, m_graphics_context, None);
  } else {
    const auto &eff = m_clip_stack.back();
    XRectangle xrect;
    xrect.x = static_cast<short>(round_to_int(eff.x));
    xrect.y = static_cast<short>(round_to_int(eff.y));
    xrect.width =
        static_cast<unsigned short>(std::max(round_to_int(eff.width), 0));
    xrect.height =
        static_cast<unsigned short>(std::max(round_to_int(eff.height), 0));
    XSetClipRectangles(m_display, m_graphics_context, 0, 0, &xrect, 1, Unsorted);
  }
}

void StudioWorkspaceRenderer::fill_rectangle(Drawable drawable,
                                             const UI::Rect &rectangle,
                                             unsigned long color) const {
  if (rectangle.is_empty()) {
    return;
  }
  XSetForeground(m_display, m_graphics_context, color);
  XFillRectangle(
      m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
      round_to_int(rectangle.y),
      static_cast<unsigned int>(std::max(round_to_int(rectangle.width), 0)),
      static_cast<unsigned int>(std::max(round_to_int(rectangle.height), 0)));
}

void StudioWorkspaceRenderer::fill_rounded_rectangle(
    Drawable drawable, const UI::Rect &rectangle, unsigned long color,
    float radius, std::optional<unsigned long> bg_color) const {
  if (rectangle.is_empty()) {
    return;
  }

  if (bg_color.has_value()) {
    unsigned long actual_bg = bg_color.value();
    unsigned long opaque_color = (255UL << 24) | (color & 0xFFFFFF);
    unsigned long opaque_bg = (255UL << 24) | (actual_bg & 0xFFFFFF);

    Utility::X11Rounded::X11Rounded::fillRoundedRectAA(
        m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
        round_to_int(rectangle.y), std::max(round_to_int(rectangle.width), 0),
        std::max(round_to_int(rectangle.height), 0), round_to_int(radius),
        opaque_color, opaque_bg, true);
  } else {
    XSetForeground(m_display, m_graphics_context, color);
    Utility::X11Rounded::X11Rounded::fillRoundedRect(
        m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
        round_to_int(rectangle.y), std::max(round_to_int(rectangle.width), 0),
        std::max(round_to_int(rectangle.height), 0), round_to_int(radius));
  }
}

void StudioWorkspaceRenderer::draw_rectangle(Drawable drawable,
                                             const UI::Rect &rectangle,
                                             unsigned long color) const {
  if (rectangle.is_empty()) {
    return;
  }
  XSetForeground(m_display, m_graphics_context, color);
  XDrawRectangle(
      m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
      round_to_int(rectangle.y),
      static_cast<unsigned int>(std::max(round_to_int(rectangle.width) - 1, 0)),
      static_cast<unsigned int>(
          std::max(round_to_int(rectangle.height) - 1, 0)));
}

void StudioWorkspaceRenderer::reset_font_drawables() noexcept {
  if (m_ui_font) m_ui_font->resetDrawable();
  if (m_small_font) m_small_font->resetDrawable();
  if (m_editor_font) m_editor_font->resetDrawable();
  if (m_minimap_font) m_minimap_font->resetDrawable();
  if (m_large_font) m_large_font->resetDrawable();
}

void StudioWorkspaceRenderer::draw_rounded_rectangle(
    Drawable drawable, const UI::Rect &rectangle, unsigned long color,
    float radius) const {
  if (rectangle.is_empty()) {
    return;
  }
  XSetForeground(m_display, m_graphics_context, color);
  Utility::X11Rounded::X11Rounded::drawRoundedRect(
      m_display, drawable, m_graphics_context, round_to_int(rectangle.x),
      round_to_int(rectangle.y), std::max(round_to_int(rectangle.width), 0),
      std::max(round_to_int(rectangle.height), 0), round_to_int(radius));
}

void StudioWorkspaceRenderer::draw_line(Drawable drawable, int from_x,
                                        int from_y, int to_x, int to_y,
                                        unsigned long color) const {
  XSetForeground(m_display, m_graphics_context, color);
  XDrawLine(m_display, drawable, m_graphics_context, from_x, from_y, to_x,
            to_y);
}

void StudioWorkspaceRenderer::draw_text(Drawable drawable,
                                        AntialiasedFont &font,
                                        std::string_view text, float point_x,
                                        float center_y,
                                        const std::string &color,
                                        const UI::Rect *clip_rect) const {
  if (text.empty()) {
    return;
  }
  const int baseline = round_to_int(
      center_y -
      static_cast<float>(font.getAscent() + font.getDescent()) * 0.5F +
      static_cast<float>(font.getAscent()));

  const UI::Rect *eff_clip = clip_rect;
  if (!eff_clip && !m_clip_stack.empty()) {
    eff_clip = &m_clip_stack.back();
  }

  if (eff_clip) {
    if (eff_clip->width <= 0.0f || eff_clip->height <= 0.0f) {
      return;
    }
    XRectangle x_clip;
    x_clip.x = static_cast<short>(eff_clip->x);
    x_clip.y = static_cast<short>(eff_clip->y);
    x_clip.width =
        static_cast<unsigned short>(std::max(0.0f, eff_clip->width));
    x_clip.height =
        static_cast<unsigned short>(std::max(0.0f, eff_clip->height));
    font.drawString(drawable, color, round_to_int(point_x), baseline,
                    std::string{text}, &x_clip);
  } else {
    font.drawString(drawable, color, round_to_int(point_x), baseline,
                    std::string{text});
  }
}

void StudioWorkspaceRenderer::draw_text(Drawable drawable,
                                        AntialiasedFont &font,
                                        std::string_view text, float point_x,
                                        float center_y,
                                        const UI::Theme::Color &color,
                                        const UI::Rect *clip_rect) const {
  draw_text(drawable, font, text, point_x, center_y, to_xft_color(color),
            clip_rect);
}

int StudioWorkspaceRenderer::get_text_width(AntialiasedFont &font,
                                            std::string_view text) const {
  if (text.empty()) {
    return 0;
  }
  return font.getTextWidth(std::string{text});
}

void StudioWorkspaceRenderer::store_cached_image(const std::string &key,
                                                 XImage *image) const {
  if (!image) {
    return;
  }
  if (m_svg_cache.size() >= max_image_cache_size) {
    auto oldest = m_svg_cache.begin();
    if (oldest != m_svg_cache.end()) {
      if (oldest->second) {
        XDestroyImage(oldest->second);
      }
      m_svg_cache.erase(oldest);
    }
  }
  m_svg_cache[key] = image;
}

void StudioWorkspaceRenderer::draw_svg_icon(
    Drawable drawable, const std::string &path, int center_x, int center_y,
    int size, const UI::Theme::Color &color, const UI::Theme::Color &background,
    bool preserve_source_colors) const {
  if (size <= 0 || m_display == nullptr || m_graphics_context == nullptr) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{path};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    std::string rel_str = resolved_path.string();
    if (rel_str.starts_with("Assets/icons/") || rel_str.starts_with("Assets\\icons\\")) {
      rel_str = rel_str.substr(13);
    } else if (rel_str.starts_with("Resources/icons/") || rel_str.starts_with("Resources\\icons\\")) {
      rel_str = rel_str.substr(16);
    } else if (rel_str.starts_with("Assets/") || rel_str.starts_with("Assets\\")) {
      rel_str = rel_str.substr(7);
    } else if (rel_str.starts_with("Resources/") || rel_str.starts_with("Resources\\")) {
      rel_str = rel_str.substr(10);
    } else if (rel_str.starts_with("vscode-codicons/icons/")) {
      // Submodule layout: codicon SVGs live under src/icons/
      rel_str = "vscode-codicons/src/icons/" + rel_str.substr(22);
    }

    const std::filesystem::path filename = resolved_path.filename();
    const std::filesystem::path direct_path = m_icon_asset_root / rel_str;
    const std::filesystem::path symbol_file_1 = m_icon_asset_root / "vscode-symbols" / "icons" / "files" / filename;
    const std::filesystem::path symbol_folder_1 = m_icon_asset_root / "vscode-symbols" / "icons" / "folders" / filename;
    const std::filesystem::path symbol_file_src = m_icon_asset_root / "vscode-symbols" / "src" / "icons" / "files" / filename;
    const std::filesystem::path symbol_folder_src = m_icon_asset_root / "vscode-symbols" / "src" / "icons" / "folders" / filename;
    const std::filesystem::path symbol_file_2 = m_icon_asset_root / "vscode-symbols" / "files" / filename;
    const std::filesystem::path symbol_folder_2 = m_icon_asset_root / "vscode-symbols" / "folders" / filename;
    const std::filesystem::path codicon_direct = m_icon_asset_root / "vscode-codicons" / "icons" / rel_str;
    const std::filesystem::path codicon_file = m_icon_asset_root / "vscode-codicons" / "icons" / filename;
    const std::filesystem::path codicon_src = m_icon_asset_root / "vscode-codicons" / "src" / "icons" / filename;
    const std::filesystem::path vsicon_file = m_icon_asset_root / "vscode-icons" / "icons" / filename;
    const std::filesystem::path material_file = m_icon_asset_root / "material-icon-theme" / filename;

    if (std::filesystem::is_regular_file(direct_path, path_error)) {
      resolved_path = direct_path;
    } else if (std::filesystem::is_regular_file(symbol_file_1, path_error)) {
      resolved_path = symbol_file_1;
    } else if (std::filesystem::is_regular_file(symbol_folder_1, path_error)) {
      resolved_path = symbol_folder_1;
    } else if (std::filesystem::is_regular_file(symbol_file_src, path_error)) {
      resolved_path = symbol_file_src;
    } else if (std::filesystem::is_regular_file(symbol_folder_src, path_error)) {
      resolved_path = symbol_folder_src;
    } else if (std::filesystem::is_regular_file(symbol_file_2, path_error)) {
      resolved_path = symbol_file_2;
    } else if (std::filesystem::is_regular_file(symbol_folder_2, path_error)) {
      resolved_path = symbol_folder_2;
    } else if (std::filesystem::is_regular_file(codicon_src, path_error)) {
      resolved_path = codicon_src;
    } else if (std::filesystem::is_regular_file(codicon_direct, path_error)) {
      resolved_path = codicon_direct;
    } else if (std::filesystem::is_regular_file(codicon_file, path_error)) {
      resolved_path = codicon_file;
    } else if (std::filesystem::is_regular_file(vsicon_file, path_error)) {
      resolved_path = vsicon_file;
    } else if (std::filesystem::is_regular_file(material_file, path_error)) {
      resolved_path = material_file;
    } else {
      const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
      if (std::filesystem::is_regular_file(themed_path, path_error)) {
        resolved_path = themed_path;
      } else {
        const std::filesystem::path legacy_path =
            m_icon_asset_root / filename;
        if (std::filesystem::is_regular_file(legacy_path, path_error)) {
          resolved_path = legacy_path;
        }
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }
  preserve_source_colors =
      preserve_source_colors &&
      (resolved_path.parent_path().filename() == "material-icon-theme" ||
       resolved_path.string().find("vscode-symbols") != std::string::npos ||
       resolved_path.string().find("vscode-icons") != std::string::npos);

  const int half = size / 2;
  const int draw_x = center_x - half;
  const int draw_y = center_y - half;

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@" + std::to_string(size) +
                                "#" + to_xft_color(color) + "/" +
                                to_xft_color(background) +
                                (preserve_source_colors ? "_p" : "");
  XImage *image = nullptr;
  auto it = m_svg_cache.find(cache_key);
  if (it != m_svg_cache.end()) {
    image = it->second;
  } else {
    auto document = lunasvg::Document::loadFromFile(resolved_string);
    if (!document) {
      return;
    }

    auto bitmap = document->renderToBitmap(static_cast<std::uint32_t>(size),
                                           static_cast<std::uint32_t>(size));
    if (bitmap.isNull()) {
      return;
    }

    char *x11_data = static_cast<char *>(std::malloc(size * size * 4));
    if (!x11_data) {
      return;
    }

    const uint32_t bg_r = static_cast<uint32_t>(background.red);
    const uint32_t bg_g = static_cast<uint32_t>(background.green);
    const uint32_t bg_b = static_cast<uint32_t>(background.blue);

    const uint32_t tint_r = static_cast<uint32_t>(color.red);
    const uint32_t tint_g = static_cast<uint32_t>(color.green);
    const uint32_t tint_b = static_cast<uint32_t>(color.blue);

    const uint32_t *src = reinterpret_cast<const uint32_t *>(bitmap.data());
    uint32_t *dst = reinterpret_cast<uint32_t *>(x11_data);

    for (int i = 0; i < size * size; ++i) {
      uint32_t pixel = src[i];
      uint32_t a = (pixel >> 24) & 0xFF;

      const uint32_t source_r = (pixel >> 16) & 0xFF;
      const uint32_t source_g = (pixel >> 8) & 0xFF;
      const uint32_t source_b = pixel & 0xFF;
      const uint32_t out_r = preserve_source_colors
                                 ? std::min(255U, source_r + (bg_r * (255 - a)) / 255)
                                 : (tint_r * a + bg_r * (255 - a)) / 255;
      const uint32_t out_g = preserve_source_colors
                                 ? std::min(255U, source_g + (bg_g * (255 - a)) / 255)
                                 : (tint_g * a + bg_g * (255 - a)) / 255;
      const uint32_t out_b = preserve_source_colors
                                 ? std::min(255U, source_b + (bg_b * (255 - a)) / 255)
                                 : (tint_b * a + bg_b * (255 - a)) / 255;

      // X11 ZPixmap expects 0x00RRGGBB on Little-Endian 24/32-bit visual
      dst[i] = (out_r << 16) | (out_g << 8) | out_b;
    }

    image = XCreateImage(
        m_display, DefaultVisual(m_display, m_screen),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)), ZPixmap,
        0, x11_data, size, size, 32, size * 4);
    if (!image) {
      std::free(x11_data);
      return;
    }
    store_cached_image(cache_key, image);
  }

  if (image) {
    XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x,
              draw_y, size, size);
  }
}

void StudioWorkspaceRenderer::draw_png_icon(
    Drawable drawable, const std::string &asset_path, int center_x,
    int center_y, int max_size, const UI::Theme::Color &background) const {
  if (asset_path.empty() || max_size <= 0 || m_display == nullptr ||
      m_graphics_context == nullptr) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_path};
  if (!std::filesystem::is_regular_file(resolved_path, path_error) && !m_icon_asset_root.empty()) {
    std::string rel_str = resolved_path.string();
    if (rel_str.starts_with("Assets/icons/") || rel_str.starts_with("Assets\\icons\\")) {
      rel_str = rel_str.substr(13);
    } else if (rel_str.starts_with("Resources/icons/") || rel_str.starts_with("Resources\\icons\\")) {
      rel_str = rel_str.substr(16);
    } else if (rel_str.starts_with("Assets/") || rel_str.starts_with("Assets\\")) {
      rel_str = rel_str.substr(7);
    } else if (rel_str.starts_with("Resources/") || rel_str.starts_with("Resources\\")) {
      rel_str = rel_str.substr(10);
    } else if (rel_str.starts_with("vscode-codicons/icons/")) {
      rel_str = "vscode-codicons/src/icons/" + rel_str.substr(22);
    }
    const std::filesystem::path direct_path = m_icon_asset_root / rel_str;
    const std::filesystem::path filename_path = m_icon_asset_root / resolved_path.filename();
    const std::filesystem::path codicon_src = m_icon_asset_root / "vscode-codicons" / "src" / "icons" / resolved_path.filename();
    const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
    if (std::filesystem::is_regular_file(direct_path, path_error)) {
      resolved_path = direct_path;
    } else if (std::filesystem::is_regular_file(codicon_src, path_error)) {
      resolved_path = codicon_src;
    } else if (std::filesystem::is_regular_file(filename_path, path_error)) {
      resolved_path = filename_path;
    } else if (std::filesystem::is_regular_file(themed_path, path_error)) {
      resolved_path = themed_path;
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return;
  }

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@png#" +
                                std::to_string(max_size) + "/" +
                                to_xft_color(background);

  XImage *image = nullptr;
  auto it = m_svg_cache.find(cache_key);
  if (it != m_svg_cache.end()) {
    image = it->second;
  } else {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *data =
        stbi_load(resolved_string.c_str(), &width, &height, &channels, 4);
    if (!data || width <= 0 || height <= 0) {
      if (data) {
        stbi_image_free(data);
      }
      return;
    }

    int draw_w = width;
    int draw_h = height;
    if (width > max_size || height > max_size) {
      const float aspect =
          static_cast<float>(width) / static_cast<float>(height);
      if (width > height) {
        draw_w = max_size;
        draw_h = std::max(1, static_cast<int>(max_size / aspect));
      } else {
        draw_h = max_size;
        draw_w = std::max(1, static_cast<int>(max_size * aspect));
      }
    }

    if (draw_w <= 0 || draw_h <= 0) {
      stbi_image_free(data);
      return;
    }

    const auto resampled = Utility::AntialiasedImage::resample_area_average(
        data, width, height, draw_w, draw_h);
    stbi_image_free(data);

    if (resampled.size() < static_cast<std::size_t>(draw_w * draw_h * 4)) {
      return;
    }

    char *x11_data = static_cast<char *>(
        std::malloc(static_cast<std::size_t>(draw_w * draw_h * 4)));
    if (!x11_data) {
      return;
    }

    const uint32_t bg_r = static_cast<uint32_t>(background.red);
    const uint32_t bg_g = static_cast<uint32_t>(background.green);
    const uint32_t bg_b = static_cast<uint32_t>(background.blue);

    uint32_t *dst = reinterpret_cast<uint32_t *>(x11_data);
    for (int i = 0; i < draw_w * draw_h; ++i) {
      const uint32_t sr = resampled[static_cast<std::size_t>(i) * 4U + 0];
      const uint32_t sg = resampled[static_cast<std::size_t>(i) * 4U + 1];
      const uint32_t sb = resampled[static_cast<std::size_t>(i) * 4U + 2];
      const uint32_t a = resampled[static_cast<std::size_t>(i) * 4U + 3];

      const uint32_t out_r = (sr * a + bg_r * (255 - a) + 127) / 255;
      const uint32_t out_g = (sg * a + bg_g * (255 - a) + 127) / 255;
      const uint32_t out_b = (sb * a + bg_b * (255 - a) + 127) / 255;

      dst[i] = (out_r << 16) | (out_g << 8) | out_b;
    }

    image = XCreateImage(
        m_display, DefaultVisual(m_display, m_screen),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)), ZPixmap,
        0, x11_data, static_cast<unsigned int>(draw_w),
        static_cast<unsigned int>(draw_h), 32, draw_w * 4);
    if (!image) {
      std::free(x11_data);
      return;
    }

    // We reuse the image cache to also store PNGs for simplicity
    store_cached_image(cache_key, image);
  }

  if (image) {
    const int draw_x = center_x - image->width / 2;
    const int draw_y = center_y - image->height / 2;
    XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x,
              draw_y, image->width, image->height);
  }
}

bool StudioWorkspaceRenderer::draw_ico_icon(
    Drawable drawable, const std::string &asset_path, int center_x,
    int center_y, int max_size, const UI::Theme::Color &background) const {
  if (asset_path.empty() || max_size <= 0 || m_display == nullptr ||
      m_graphics_context == nullptr) {
    return false;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_path};
  if (resolved_path.is_relative() && !m_icon_asset_root.empty()) {
    const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
    if (std::filesystem::is_regular_file(themed_path, path_error)) {
      resolved_path = themed_path;
    } else {
      // Keep compatibility with callers that pass Assets/icons/foo.ico.
      const std::filesystem::path legacy_path =
          m_icon_asset_root / resolved_path.filename();
      if (std::filesystem::is_regular_file(legacy_path, path_error)) {
        resolved_path = legacy_path;
      }
    }
  }
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    return false;
  }

  const std::string resolved_string = resolved_path.string();
  const std::string cache_key = resolved_string + "@ico#" +
                                std::to_string(max_size) + "/" +
                                to_xft_color(background);

  XImage *image = nullptr;
  auto it = m_svg_cache.find(cache_key);
  if (it != m_svg_cache.end()) {
    image = it->second;
  } else {
    auto decoded = Utility::decode_ico_file(resolved_string);
    if (!decoded || decoded->width <= 0 || decoded->height <= 0 ||
        decoded->pixels.empty()) {
      return false;
    }

    const int width = decoded->width;
    const int height = decoded->height;

    // Compute size to fit in max_size.
    int draw_w = width;
    int draw_h = height;
    if (width > max_size || height > max_size) {
      const float aspect =
          static_cast<float>(width) / static_cast<float>(height);
      if (width > height) {
        draw_w = max_size;
        draw_h = std::max(1, static_cast<int>(max_size / aspect));
      } else {
        draw_h = max_size;
        draw_w = std::max(1, static_cast<int>(max_size * aspect));
      }
    }

    const auto resampled = Utility::AntialiasedImage::resample_area_average(
        decoded->pixels.data(), width, height, draw_w, draw_h);

    if (resampled.size() < static_cast<std::size_t>(draw_w * draw_h * 4)) {
      return false;
    }

    char *x11_data = static_cast<char *>(
        std::malloc(static_cast<std::size_t>(draw_w * draw_h * 4)));
    if (!x11_data) {
      return false;
    }

    const uint32_t bg_r = static_cast<uint32_t>(background.red);
    const uint32_t bg_g = static_cast<uint32_t>(background.green);
    const uint32_t bg_b = static_cast<uint32_t>(background.blue);

    uint32_t *dst = reinterpret_cast<uint32_t *>(x11_data);
    for (int i = 0; i < draw_w * draw_h; ++i) {
      const uint32_t sr = resampled[static_cast<std::size_t>(i) * 4U + 0];
      const uint32_t sg = resampled[static_cast<std::size_t>(i) * 4U + 1];
      const uint32_t sb = resampled[static_cast<std::size_t>(i) * 4U + 2];
      const uint32_t a = resampled[static_cast<std::size_t>(i) * 4U + 3];

      const uint32_t out_r = (sr * a + bg_r * (255 - a) + 127) / 255;
      const uint32_t out_g = (sg * a + bg_g * (255 - a) + 127) / 255;
      const uint32_t out_b = (sb * a + bg_b * (255 - a) + 127) / 255;

      dst[i] = (out_r << 16) | (out_g << 8) | out_b;
    }

    image = XCreateImage(
        m_display, DefaultVisual(m_display, m_screen),
        static_cast<unsigned int>(DefaultDepth(m_display, m_screen)), ZPixmap,
        0, x11_data, static_cast<unsigned int>(draw_w),
        static_cast<unsigned int>(draw_h), 32, draw_w * 4);
    if (!image) {
      std::free(x11_data);
      return false;
    }

    // Reuse the SVG/PNG cache for ICOs as well.
    store_cached_image(cache_key, image);
  }

  const int draw_x = center_x - image->width / 2;
  const int draw_y = center_y - image->height / 2;
  XPutImage(m_display, drawable, m_graphics_context, image, 0, 0, draw_x,
            draw_y, image->width, image->height);
  return true;
}

void StudioWorkspaceRenderer::apply_theme(const UI::Theme::StudioTheme &theme) {
  m_theme = theme;
  m_palette = UI::Editor::StudioEditorPalette::from_theme(theme);
  m_pixels.workspace_background = allocate_color(m_palette.workspace_background);
  m_pixels.tab_background = allocate_color(m_palette.tab_background);
  m_pixels.tab_active_background = allocate_color(m_palette.tab_active_background);
  m_pixels.sidebar_background = allocate_color(m_palette.sidebar_background);
  m_pixels.editor_background = allocate_color(m_palette.editor_background);
  m_pixels.active_line_background = allocate_color(m_palette.active_line_background);
  m_pixels.selection_background = allocate_color(m_palette.selection_background);
  m_pixels.status_background = allocate_color(m_palette.status_background);
  m_pixels.border = allocate_color(m_palette.border);
  m_pixels.text_primary = allocate_color(m_palette.text_primary);
  m_pixels.text_muted = allocate_color(m_palette.text_muted);
  m_pixels.accent = allocate_color(m_palette.accent);
  m_pixels.warning = allocate_color(m_palette.warning);
  m_pixels.success = allocate_color(m_palette.success);
  m_pixels.hover_background = allocate_color(m_palette.hover_background);
  m_pixels.indent_guide = allocate_color(m_palette.indent_guide);
  m_pixels.indent_guide_active = allocate_color(m_palette.indent_guide_active);

  m_text.primary = to_xft_color(m_palette.text_primary);
  m_text.muted = to_xft_color(m_palette.text_muted);
  m_text.keyword = to_xft_color(m_palette.keyword);
  m_text.number = to_xft_color(m_palette.number);
  m_text.label = to_xft_color(m_palette.label);
  m_text.type = to_xft_color(m_palette.type);
  m_text.comment = to_xft_color(m_palette.comment);
  m_text.accent = to_xft_color(m_palette.accent);
  m_text.warning = to_xft_color(m_palette.warning);
  m_text.success = to_xft_color(m_palette.success);

  m_text_dimmed.primary = to_xft_color(UI::Theme::dim_color(m_palette.text_primary, m_palette.editor_background));
  m_text_dimmed.muted = to_xft_color(UI::Theme::dim_color(m_palette.text_muted, m_palette.editor_background));
  m_text_dimmed.keyword = to_xft_color(UI::Theme::dim_color(m_palette.keyword, m_palette.editor_background));
  m_text_dimmed.number = to_xft_color(UI::Theme::dim_color(m_palette.number, m_palette.editor_background));
  m_text_dimmed.label = to_xft_color(UI::Theme::dim_color(m_palette.label, m_palette.editor_background));
  m_text_dimmed.type = to_xft_color(UI::Theme::dim_color(m_palette.type, m_palette.editor_background));
  m_text_dimmed.comment = to_xft_color(UI::Theme::dim_color(m_palette.comment, m_palette.editor_background));
  m_text_dimmed.accent = to_xft_color(UI::Theme::dim_color(m_palette.accent, m_palette.editor_background));
  m_text_dimmed.warning = to_xft_color(UI::Theme::dim_color(m_palette.warning, m_palette.editor_background));
  m_text_dimmed.success = to_xft_color(UI::Theme::dim_color(m_palette.success, m_palette.editor_background));

  m_settings_window.set_theme(theme);
}

} // namespace Zenvra::Platform::X11::Components

