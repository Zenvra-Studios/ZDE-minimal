#include "UI/Editor/StudioEditorModel.h"

#include "Utility/Column.h"
#include "Utility/Row.h"

#include "Language/Syntax/GenericGrammarEngine.h"
#include "Language/Syntax/GrammarRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace Zenvra::UI::Editor {

namespace {

constexpr std::array<SidebarItem, 7> sidebar_items{
    SidebarItem{"project", "Project", SidebarIcon::Project,
                SidebarPlacement::Top, true},
    SidebarItem{"version-control", "Version Control",
                SidebarIcon::VersionControl, SidebarPlacement::Top, false},
    SidebarItem{"search", "Search", SidebarIcon::Search, SidebarPlacement::Top,
                false},
    SidebarItem{"services", "Services", SidebarIcon::Services,
                SidebarPlacement::Top, false},
    SidebarItem{"shader", "Shader Sandbox", SidebarIcon::Shader,
                SidebarPlacement::Top, false},
    SidebarItem{"more", "More Tool Windows", SidebarIcon::More,
                SidebarPlacement::Top, false},
    SidebarItem{"terminal", "Terminal", SidebarIcon::Terminal,
                SidebarPlacement::Bottom, false},
};

constexpr auto keywords = std::to_array<std::string_view>({
    "alignas",
    "alignof",
    "and",
    "and_eq",
    "asm",
    "auto",
    "bitand",
    "bitor",
    "break",
    "case",
    "catch",
    "class",
    "compl",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "co_await",
    "co_return",
    "co_yield",
    "decltype",
    "default",
    "delete",
    "do",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "nullptr",
    "operator",
    "or",
    "or_eq",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "using",
    "virtual",
    "volatile",
    "while",
    "xor",
    "xor_eq",
});

constexpr auto types = std::to_array<std::string_view>({
    "bool",    "char",    "char8_t",    "char16_t",  "char32_t",    "double",
    "float",   "int",     "long",       "short",     "signed",      "unsigned",
    "void",    "wchar_t", "size_t",     "string",    "string_view", "Display",
    "Runtime", "Window",  "X11Context", "X11Window",
});

bool is_identifier_start(char character) {
  return std::isalpha(static_cast<unsigned char>(character)) != 0 ||
         character == '_';
}

bool is_identifier_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
         character == '_' || character == '.';
}

bool contains(std::span<const std::string_view> values,
              std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

StudioEditorPalette StudioEditorPalette::jetbrains_dark() noexcept {
  return StudioEditorPalette{
      .workspace_background = {30, 31, 34, 255},
      .tab_background = {29, 30, 33, 255},
      .tab_active_background = {31, 32, 35, 255},
      .sidebar_background = {28, 29, 32, 255},
      .editor_background = {30, 31, 34, 255},
      .active_line_background = {36, 37, 42, 255},
      .selection_background = {48, 70, 101, 255},
      .status_background = {30, 31, 34, 255},
      .border = {48, 50, 55, 255},
      .text_primary = {188, 190, 196, 255},
      .text_muted = {104, 107, 115, 255},
      .keyword = {207, 145, 105, 255},
      .number = {86, 181, 172, 255},
      .label = {210, 126, 164, 255},
      .type = {110, 174, 194, 255},
      .comment = {98, 102, 110, 255},
      .accent = {53, 132, 228, 255},
      .warning = {219, 139, 72, 255},
      .success = {83, 157, 84, 255},
      .tooltip_background = {43, 45, 49, 255},
      .hover_background = {53, 53, 56, 255},
      .indent_guide = {48, 50, 56, 255},
      .indent_guide_active = {95, 100, 112, 255},
  };
}

StudioEditorLayoutResult StudioEditorLayout::calculate(
    float client_width, float client_height, float content_top, float dpi_scale,
    bool terminal_visible, float terminal_height, bool terminal_maximized,
    bool tool_sidebar_visible, float tool_sidebar_width,
    bool shader_panel_visible, float shader_panel_width,
    std::optional<float> custom_nav_width) const noexcept {
  const float safe_width = std::max(client_width, 0.0F);
  const float safe_height = std::max(client_height, 0.0F);
  const float safe_scale = std::max(dpi_scale, 0.5F);
  const float safe_top = std::clamp(content_top, 0.0F, safe_height);
  const float activity_width = StudioEditorMetrics::activity_width * safe_scale;
  const float status_height = StudioEditorMetrics::status_height * safe_scale;
  const float gutter_width = StudioEditorMetrics::gutter_width * safe_scale;
  const float scrollbar_width = 14.0F * safe_scale;
  const float splitter_width = shader_panel_visible ? (4.0F * safe_scale) : 0.0F;

  const float available_after_activity =
      std::max(safe_width - activity_width, 0.0F);
  const float minimum_editor_region =
      std::min(240.0F * safe_scale, available_after_activity);

  const float requested_shader_width = shader_panel_visible
      ? std::clamp(shader_panel_width * safe_scale, 180.0F * safe_scale,
                   std::max(available_after_activity - minimum_editor_region - splitter_width, 0.0F))
      : 0.0F;

  const float remaining_for_sidebar = std::max(
      available_after_activity - minimum_editor_region - requested_shader_width - splitter_width,
      0.0F);
  const float sidebar_width = tool_sidebar_visible
                                  ? std::clamp(tool_sidebar_width * safe_scale,
                                               0.0F, remaining_for_sidebar)
                                  : 0.0F;

  // The tab strip fills the titlebar edge-to-edge. If there is no custom chrome
  // (safe_top == 0), we allocate a dedicated tab bar at the top of the content.
  const float effective_tab_height = (safe_top > 0.0F) ? safe_top : (StudioEditorMetrics::tab_height * safe_scale);
  const float root_y = (safe_top > 0.0F) ? safe_top : effective_tab_height;

  const UI::Rect root_bounds{
      0.0F,
      root_y,
      safe_width,
      std::max(safe_height - root_y, 0.0F),
  };
  const std::array root_items{
      Utility::FlexItem::flexible(),
      Utility::FlexItem::fixed(status_height),
  };
  const Utility::FlexLayoutResult root =
      Utility::Column::calculate(root_bounds, root_items);
  const UI::Rect content_bounds = root.items[0];
  const UI::Rect status_bounds = root.items[1];

  const std::array content_items{
      Utility::FlexItem::fixed(activity_width),
      Utility::FlexItem::fixed(sidebar_width),
      Utility::FlexItem::flexible(),
      Utility::FlexItem::fixed(splitter_width),
      Utility::FlexItem::fixed(requested_shader_width),
  };
  const Utility::FlexLayoutResult content =
      Utility::Row::calculate(content_bounds, content_items);
  const UI::Rect activity_bounds = content.items[0];
  const UI::Rect sidebar_bounds = content.items[1];
  const UI::Rect editor_workspace_bounds = content.items[2];
  const UI::Rect shader_splitter_bounds = content.items[3];
  const UI::Rect shader_panel_bounds = content.items[4];

  const float integrated_tab_y = 0.0F;
  const float integrated_tab_height = effective_tab_height;
  
  const float nav_width = (safe_top > 0.0F)
      ? (custom_nav_width.has_value()
             ? std::max(0.0F, *custom_nav_width)
             : StudioEditorMetrics::titlebar_navigation_width * safe_scale)
      : 0.0F;

  // Responsive right toolbar controls width calculation:
  // On macOS: traffic lights are on the LEFT, so the right side only holds the action toolbar (~550px * scale) + padding.
  const bool is_compact_window = safe_width < (960.0F * safe_scale);
  const bool is_ultra_compact_window = safe_width < (760.0F * safe_scale);

  const float base_toolbar_width = is_ultra_compact_window
      ? (410.0F * safe_scale)
      : (is_compact_window ? (486.0F * safe_scale) : (564.0F * safe_scale));

  const float platform_controls_width =
#if defined(__APPLE__)
      base_toolbar_width;
#else
      base_toolbar_width + (138.0F * safe_scale);
#endif

  const float ctrl_width = (safe_top > 0.0F) ? platform_controls_width : 0.0F;
  
  const float integrated_tab_x = std::min(nav_width, safe_width);
  const float integrated_tab_right = (safe_width > (integrated_tab_x + ctrl_width))
                                         ? (safe_width - ctrl_width)
                                         : integrated_tab_x;
  const UI::Rect tab_bounds{
      integrated_tab_x,
      integrated_tab_y,
      std::max(0.0F, integrated_tab_right - integrated_tab_x),
      integrated_tab_height,
  };
  const UI::Rect workspace_body = editor_workspace_bounds;

  const float requested_terminal_height =
      terminal_visible
          ? (terminal_maximized ? workspace_body.height
                                : std::clamp(terminal_height * safe_scale, 0.0F,
                                             workspace_body.height))
          : 0.0F;
  const std::array body_items{
      Utility::FlexItem::flexible(),
      Utility::FlexItem::fixed(requested_terminal_height),
  };
  const Utility::FlexLayoutResult body =
      Utility::Column::calculate(workspace_body, body_items);
  const UI::Rect editor_row_bounds = body.items[0];
  const UI::Rect terminal_bounds = body.items[1];

  const float editor_header_height =
      std::min(StudioEditorMetrics::editor_header_height * safe_scale, editor_row_bounds.height);
  const std::array editor_vertical_items{
      Utility::FlexItem::fixed(editor_header_height),
      Utility::FlexItem::flexible(),
  };
  const Utility::FlexLayoutResult editor_vertical =
      Utility::Column::calculate(editor_row_bounds, editor_vertical_items);
  const UI::Rect editor_header_bounds = editor_vertical.items[0];
  const UI::Rect editor_code_bounds = editor_vertical.items[1];

  const std::array editor_items{
      Utility::FlexItem::fixed(gutter_width),
      Utility::FlexItem::flexible(),
  };
  const Utility::FlexLayoutResult editor =
      Utility::Row::calculate(editor_code_bounds, editor_items);

  const float terminal_header_height =
      std::min(28.0F * safe_scale, terminal_bounds.height);
  const std::array terminal_items{
      Utility::FlexItem::fixed(terminal_header_height),
      Utility::FlexItem::flexible(),
  };
  const Utility::FlexLayoutResult terminal =
      Utility::Column::calculate(terminal_bounds, terminal_items);

  const float shader_header_height = std::min(28.0F * safe_scale, shader_panel_bounds.height);
  const float shader_controls_height = std::min(36.0F * safe_scale, std::max(shader_panel_bounds.height - shader_header_height, 0.0F));
  const std::array shader_sub_items{
      Utility::FlexItem::fixed(shader_header_height),
      Utility::FlexItem::flexible(),
      Utility::FlexItem::fixed(shader_controls_height),
  };
  const Utility::FlexLayoutResult shader_layout =
      Utility::Column::calculate(shader_panel_bounds, shader_sub_items);

  StudioEditorLayoutResult result;
  result.dpi_scale = safe_scale;
  result.workspace_bounds = root_bounds;
  result.tab_bar_bounds = tab_bounds;
  result.activity_bar_bounds = activity_bounds;
  result.tool_sidebar_bounds = sidebar_bounds;
  result.editor_header_bounds = editor_header_bounds;
  result.gutter_bounds = editor.items[0];
  result.editor_bounds = editor.items[1];
  result.terminal_panel_bounds = terminal_bounds;
  result.terminal_header_bounds = terminal.items[0];
  result.terminal_content_bounds = terminal.items[1];
  result.status_bar_bounds = status_bounds;
  result.shader_panel_bounds = shader_panel_bounds;
  result.shader_panel_header_bounds = shader_layout.items[0];
  result.shader_panel_viewport_bounds = shader_layout.items[1];
  result.shader_panel_controls_bounds = shader_layout.items[2];
  result.shader_splitter_bounds = shader_splitter_bounds;
  result.shader_panel_visible = shader_panel_visible;

  const float scroll_top_y = result.editor_bounds.y;
  const float scroll_total_h = result.editor_bounds.height;
  const float minimap_width = (result.editor_bounds.width >= 120.0F * safe_scale)
      ? (112.0F * safe_scale)
      : 0.0F;
  result.minimap_bounds = {
      std::max(result.editor_bounds.right() - scrollbar_width - minimap_width,
               result.editor_bounds.x),
      scroll_top_y,
      std::min(minimap_width,
               std::max(result.editor_bounds.width - scrollbar_width,
                        0.0F)),
      scroll_total_h,
  };
  result.scrollbar_bounds = {
      std::max(result.editor_bounds.right() - scrollbar_width, result.editor_bounds.x),
      scroll_top_y,
      std::min(scrollbar_width, result.editor_bounds.width),
      scroll_total_h,
  };

  return result;
}

std::span<const SidebarItem> get_studio_sidebar_items() noexcept {
  return sidebar_items;
}

Rect calculate_studio_sidebar_item_bounds(
    const StudioEditorLayoutResult &layout, std::size_t item_index) noexcept {
  const std::span<const SidebarItem> items = get_studio_sidebar_items();
  if (item_index >= items.size()) {
    return {};
  }
  const float scale = layout.dpi_scale;
  const SidebarItem &target = items[item_index];
  std::size_t placement_index = 0;
  std::size_t placement_count = 0;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (items[index].placement == target.placement) {
      if (index < item_index) {
        ++placement_index;
      }
      ++placement_count;
    }
  }
  float center_y = 0.0F;
  if (target.placement == SidebarPlacement::Top) {
    const bool tabs_are_in_titlebar =
        layout.tab_bar_bounds.bottom() <= layout.activity_bar_bounds.y;
    center_y =
        tabs_are_in_titlebar
            ? layout.activity_bar_bounds.y +
                  (StudioEditorMetrics::tab_height * 0.5F +
                   static_cast<float>(placement_index) *
                       StudioEditorMetrics::sidebar_item_spacing) *
                      scale
        : placement_index == 0
            ? layout.tab_bar_bounds.y + layout.tab_bar_bounds.height * 0.5F
            : layout.editor_bounds.y +
                  (StudioEditorMetrics::sidebar_top_offset +
                   static_cast<float>(placement_index - 1) *
                       StudioEditorMetrics::sidebar_item_spacing) *
                      scale;
  } else {
    const std::size_t reverse_index = placement_count - placement_index;
    center_y = layout.status_bar_bounds.y -
               (StudioEditorMetrics::sidebar_bottom_offset +
                static_cast<float>(reverse_index - 1) *
                    StudioEditorMetrics::sidebar_item_spacing) *
                   scale;
  }
  return Rect{
      layout.activity_bar_bounds.x,
      center_y - StudioEditorMetrics::sidebar_item_height * 0.5F * scale,
      layout.activity_bar_bounds.width,
      StudioEditorMetrics::sidebar_item_height * scale,
  };
}

std::optional<std::size_t>
hit_test_studio_sidebar(const StudioEditorLayoutResult &layout, float point_x,
                        float point_y) noexcept {
  const std::span<const SidebarItem> items = get_studio_sidebar_items();
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (calculate_studio_sidebar_item_bounds(layout, index)
            .contains(point_x, point_y)) {
      return index;
    }
  }
  return std::nullopt;
}

float calculate_editor_tab_width(float text_width, float dpi_scale) noexcept {
  const float scale = std::max(dpi_scale, 0.5F);
  return std::clamp(std::max(text_width, 0.0F) +
                        StudioEditorMetrics::editor_tab_chrome_width * scale,
                    StudioEditorMetrics::editor_tab_minimum_width * scale,
                    StudioEditorMetrics::editor_tab_maximum_width * scale);
}

std::size_t tokenize_editor_line(
    std::string_view line,
    std::array<EditorToken, maximum_editor_tokens> &output,
    std::string_view file_name) noexcept {
  const auto* grammar = file_name.empty()
      ? Language::Syntax::GrammarRegistry::instance().get_grammar_for_extension(".cpp")
      : Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename(file_name);
  if (grammar != nullptr) {
    return Language::Syntax::GenericGrammarEngine::tokenize_line(line, *grammar, output);
  }
  if (!line.empty() && !output.empty()) {
    output[0] = EditorToken{line, EditorTokenKind::Plain};
    return 1;
  }
  return 0;
}

bool supports_editor_syntax_highlighting(std::string_view file_name) noexcept {
  const auto* grammar = Language::Syntax::GrammarRegistry::instance().get_grammar_for_filename(file_name);
  if (grammar != nullptr) {
    return true;
  }

  std::string normalized_name{file_name};
  std::transform(normalized_name.begin(), normalized_name.end(),
                 normalized_name.begin(), [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  if (normalized_name.ends_with(".log") || normalized_name.ends_with(".txt")) {
    return false;
  }

  return false;
}

} // namespace Zenvra::UI::Editor
