#include "Platform/X11/Components/StudioWorkspaceRenderer.h"
#include "Commands/CommandIds.h"
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
  const std::filesystem::path current_path =
      std::filesystem::current_path(path_error);
  std::optional<std::filesystem::path> project_root;
  if (!path_error) {
    project_root =
        UI::Editor::EditorFileSystem::find_project_root(current_path);
  }
  if (!project_root) {
    path_error.clear();
    const std::filesystem::path executable_path =
        std::filesystem::read_symlink("/proc/self/exe", path_error);
    if (!path_error) {
      project_root =
          UI::Editor::EditorFileSystem::find_project_root(executable_path);
    }
  }
  if (project_root) {
    m_icon_asset_root = *project_root / "Assets" / "icons";
  }
  m_graphics_context =
      XCreateGC(m_display, RootWindow(m_display, m_screen), 0, nullptr);
  if (m_graphics_context == nullptr) {
    shutdown();
    return false;
  }

  // Load bundled fonts from Assets/fonts/. For each valid TTF font, register
  // the file with Fontconfig.
  bool custom_font_loaded = false;
  if (project_root) {
    const std::filesystem::path hack_dir =
        *project_root / "Assets" / "fonts" / "Hack" / "ttf";
    const std::filesystem::path jb_dir =
        *project_root / "Assets" / "fonts" / "JetBrainsMono";
    std::error_code ec;

    if (std::filesystem::is_directory(hack_dir, ec)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(hack_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".ttf" &&
            entry.file_size(ec) > 1000) {
          FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(
                                              entry.path().string().c_str()));
        }
      }
    }

    if (std::filesystem::is_directory(jb_dir, ec)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(jb_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".ttf" &&
            entry.file_size(ec) > 1000) {
          FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(
                                              entry.path().string().c_str()));
        }
      }
    }

    const std::filesystem::path opensans_dir = *project_root / "Assets" / "fonts" / "OpenSans";
    if (std::filesystem::is_directory(opensans_dir, ec)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(opensans_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".ttf" &&
            entry.file_size(ec) > 1000) {
          FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8 *>(
                                              entry.path().string().c_str()));
        }
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
  static_cast<void>(m_tool_sidebar.initialize());
  static_cast<void>(m_terminal_panel.toggle());
  static_cast<void>(m_shader_sandbox_panel.initialize());
  m_terminal_panel.set_focused(false);
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

bool StudioWorkspaceRenderer::set_workspace_root(
    const std::filesystem::path &root) {
  if (!m_tool_sidebar.set_workspace_root(root)) {
    return false;
  }
  m_terminal_panel.set_working_directory(root);
  return true;
}

bool StudioWorkspaceRenderer::close_project() {
  m_text_editor.close_all_files();
  m_tool_sidebar.clear_workspace();
  m_terminal_panel.set_working_directory({});
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
    std::string &command_out) {
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
        sync_shader_sandbox();
      }
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
    }
    return true;
  }
  if (m_terminal_panel.handle_pointer_press(layout, point_x, point_y,
                                            event_time, click_count,
                                            extend_selection)) {
    return true;
  }
  if (m_shader_sandbox_panel.is_visible() &&
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    return true;
  }
  m_terminal_panel.set_focused(false);
  const bool editor_pressed = m_text_editor.handle_pointer_press(
      *this, layout, point_x, point_y, extend_selection, click_count,
      command_out);
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
  return sidebar_changed || terminal_changed || editor_changed ||
         shader_changed;
}

bool StudioWorkspaceRenderer::handle_pointer_drag(float point_x, float point_y,
                                                  int client_width,
                                                  int client_height,
                                                  float content_top) {
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  if (m_tool_sidebar.is_resizing() || m_tool_sidebar.is_dragging_item()) {
    return m_tool_sidebar.handle_pointer_drag(layout, point_x, point_y);
  }
  if (m_terminal_panel.is_resizing()) {
    return m_terminal_panel.handle_pointer_drag(layout, point_y);
  }
  if (m_terminal_panel.handle_pointer_drag(layout, point_x, point_y)) {
    return true;
  }
  if (m_shader_sandbox_panel.is_resizing() ||
      m_shader_sandbox_panel.contains(layout, point_x, point_y)) {
    if (m_shader_sandbox_panel.handle_pointer_drag(layout, point_x, point_y)) {
      return true;
    }
  }
  return m_text_editor.handle_pointer_drag(*this, layout, point_x, point_y);
}

bool StudioWorkspaceRenderer::handle_pointer_release() noexcept {
  const bool terminal_changed = m_terminal_panel.handle_pointer_release();
  const bool sidebar_changed = m_tool_sidebar.handle_pointer_release();
  const bool editor_changed = m_text_editor.handle_pointer_release();
  const bool shader_changed = m_shader_sandbox_panel.handle_pointer_release();
  return terminal_changed || sidebar_changed || editor_changed ||
         shader_changed;
}

bool StudioWorkspaceRenderer::handle_scroll(float point_x, float point_y,
                                            std::string &command_out,
                                            std::ptrdiff_t line_delta,
                                            bool horizontal, int client_width,
                                            int client_height,
                                            float content_top) noexcept {
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
  m_shader_panel_visible = false;
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
  if (command_id == Commands::CommandIds::view_toggle_bottom_dock ||
      command_id == Commands::CommandIds::view_terminal_panel ||
      command_id == Commands::CommandIds::view_output ||
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
  if (command_id == Commands::CommandIds::view_search) {
    m_tool_sidebar.get_model().set_visible(true);
    static_cast<void>(m_tool_sidebar.activate(UI::Editor::SidebarIcon::Search));
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
      command_id == Commands::CommandIds::open_plugins) {
    return true;
  }
  return m_text_editor.is_command_enabled(command_id);
}

bool StudioWorkspaceRenderer::handle_text_input(std::string_view utf8_text) {
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
  return (layout.gutter_bounds.contains(point_x, point_y) ||
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

bool StudioWorkspaceRenderer::is_empty_state_button_hovered() const noexcept {
  return m_text_editor.is_empty_state_button_hovered();
}

bool StudioWorkspaceRenderer::tick_animations() noexcept {
  const bool caret_changed = m_text_editor.tick_animations();
  const bool terminal_changed = m_terminal_panel.poll();
  const bool terminal_blink_changed = m_terminal_panel.tick_animations();
  const bool shader_changed = m_shader_sandbox_panel.tick_animations();
  return caret_changed || terminal_changed || terminal_blink_changed ||
         shader_changed;
}

void StudioWorkspaceRenderer::shutdown() {
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
  return m_layout_engine.calculate(
      static_cast<float>(client_width), static_cast<float>(client_height),
      content_top, m_dpi_scale, m_terminal_panel.is_visible(),
      m_terminal_panel.get_height(), m_terminal_panel.is_maximized(),
      m_tool_sidebar.is_visible(), m_tool_sidebar.get_width(),
      m_shader_sandbox_panel.is_visible(), m_shader_sandbox_panel.get_width());
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
  const UI::Editor::StudioEditorLayoutResult layout =
      calculate_layout(client_width, client_height, content_top);
  fill_rectangle(drawable, layout.workspace_bounds,
                 m_pixels.workspace_background);
  fill_rectangle(drawable, layout.tab_bar_bounds, m_pixels.tab_background);
  fill_rectangle(drawable, layout.activity_bar_bounds,
                 m_pixels.sidebar_background);
  fill_rectangle(drawable, layout.tool_sidebar_bounds,
                 m_pixels.sidebar_background);
  fill_rectangle(drawable, layout.editor_header_bounds,
                 m_pixels.editor_background);
  fill_rectangle(drawable, layout.gutter_bounds, m_pixels.editor_background);
  fill_rectangle(drawable, layout.editor_bounds, m_pixels.editor_background);
  fill_rectangle(drawable, layout.status_bar_bounds,
                 m_pixels.status_background);

  m_text_editor.render(*this, drawable, layout);
  m_terminal_panel.render(*this, drawable, layout);
  m_tool_sidebar.render(*this, drawable, layout);
  m_activity_sidebar.render(*this, drawable, layout);
  m_shader_sandbox_panel.render(*this, drawable, layout);
  if (const UI::Editor::TextDocumentModel *document =
          m_text_editor.get_document()) {
    const std::vector<UI::Editor::BreadcrumbItem> full_breadcrumbs =
        document->get_full_breadcrumbs();
    m_footer_toolbar.render(*this, drawable, layout, full_breadcrumbs,
                            document->get_status());
  }
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
    const UI::Theme::Color input_border{0, 122, 204, 255}; // Accent Blue Border
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

    // Draw Caret
    const int text_w = m_editor_font->getTextWidth(text);
    const float caret_x = text_x + static_cast<float>(text_w);
    draw_line(drawable, round_to_int(caret_x),
              round_to_int(layout.input_bounds.y + 5.0F * m_dpi_scale),
              round_to_int(caret_x),
              round_to_int(layout.input_bounds.bottom() - 5.0F * m_dpi_scale),
              m_pixels.text_primary);
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
  XRectangle xrect;
  xrect.x = static_cast<short>(round_to_int(rect.x));
  xrect.y = static_cast<short>(round_to_int(rect.y));
  xrect.width =
      static_cast<unsigned short>(std::max(round_to_int(rect.width), 0));
  xrect.height =
      static_cast<unsigned short>(std::max(round_to_int(rect.height), 0));
  XSetClipRectangles(m_display, m_graphics_context, 0, 0, &xrect, 1, Unsorted);
}

void StudioWorkspaceRenderer::pop_clip() const {
  XSetClipMask(m_display, m_graphics_context, None);
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

  if (clip_rect) {
    XRectangle x_clip;
    x_clip.x = static_cast<short>(clip_rect->x);
    x_clip.y = static_cast<short>(clip_rect->y);
    x_clip.width =
        static_cast<unsigned short>(std::max(0.0f, clip_rect->width));
    x_clip.height =
        static_cast<unsigned short>(std::max(0.0f, clip_rect->height));
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
    }

    const std::filesystem::path direct_path = m_icon_asset_root / rel_str;
    const std::filesystem::path codicon_direct = m_icon_asset_root / "vscode-codicons" / "icons" / rel_str;
    const std::filesystem::path codicon_file = m_icon_asset_root / "vscode-codicons" / "icons" / resolved_path.filename();
    if (std::filesystem::is_regular_file(direct_path, path_error)) {
      resolved_path = direct_path;
    } else if (std::filesystem::is_regular_file(codicon_direct, path_error)) {
      resolved_path = codicon_direct;
    } else if (std::filesystem::is_regular_file(codicon_file, path_error)) {
      resolved_path = codicon_file;
    } else {
      const std::filesystem::path themed_path = m_icon_asset_root / resolved_path;
      if (std::filesystem::is_regular_file(themed_path, path_error)) {
        resolved_path = themed_path;
      } else {
        const std::filesystem::path legacy_path =
            m_icon_asset_root / resolved_path.filename();
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
       resolved_path.string().find("vscode-symbols") != std::string::npos);

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
    if (m_svg_cache.size() >= 128) {
      for (auto &[k, img] : m_svg_cache) {
        if (img) XDestroyImage(img);
      }
      m_svg_cache.clear();
    }
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
        0, x11_data, size, size, 32, 0);
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
  if (asset_path.empty()) {
    return;
  }

  std::error_code path_error;
  std::filesystem::path resolved_path{asset_path};
  if (!std::filesystem::is_regular_file(resolved_path, path_error)) {
    resolved_path = m_icon_asset_root / resolved_path;
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
    if (!data) {
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

    const auto resampled = Utility::AntialiasedImage::resample_area_average(
        data, width, height, draw_w, draw_h);
    stbi_image_free(data);

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
        0, x11_data, draw_w, draw_h, 32, 0);
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
        0, x11_data, draw_w, draw_h, 32, 0);
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

} // namespace Zenvra::Platform::X11::Components
