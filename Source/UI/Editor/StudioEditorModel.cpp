#include "UI/Editor/StudioEditorModel.h"

#include "Utility/Column.h"
#include "Utility/Row.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace Zenvra::UI::Editor {

namespace {

constexpr std::array<SidebarItem, 6> sidebar_items{
    SidebarItem{"project", "Project", SidebarIcon::Project,
                SidebarPlacement::Top, true},
    SidebarItem{"version-control", "Version Control",
                SidebarIcon::VersionControl, SidebarPlacement::Top, false},
    SidebarItem{"search", "Search", SidebarIcon::Search, SidebarPlacement::Top,
                false},
    SidebarItem{"services", "Services", SidebarIcon::Services,
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
  };
}

StudioEditorLayoutResult StudioEditorLayout::calculate(
    float client_width, float client_height, float content_top, float dpi_scale,
    bool terminal_visible, float terminal_height, bool terminal_maximized,
    bool tool_sidebar_visible, float tool_sidebar_width) const noexcept {
  const float safe_width = std::max(client_width, 0.0F);
  const float safe_height = std::max(client_height, 0.0F);
  const float safe_scale = std::max(dpi_scale, 0.5F);
  const float safe_top = std::clamp(content_top, 0.0F, safe_height);
  const float activity_width = StudioEditorMetrics::activity_width * safe_scale;
  const float status_height = StudioEditorMetrics::status_height * safe_scale;
  const float gutter_width = StudioEditorMetrics::gutter_width * safe_scale;
  const float scrollbar_width = 14.0F * safe_scale;
  const float available_after_activity =
      std::max(safe_width - activity_width, 0.0F);
  const float minimum_editor_region =
      std::min(240.0F * safe_scale, available_after_activity);
  const float maximum_sidebar_width =
      std::max(available_after_activity - minimum_editor_region, 0.0F);
  const float sidebar_width = tool_sidebar_visible
                                  ? std::clamp(tool_sidebar_width * safe_scale,
                                               0.0F, maximum_sidebar_width)
                                  : 0.0F;

  const UI::Rect root_bounds{
      0.0F,
      safe_top,
      safe_width,
      std::max(safe_height - safe_top, 0.0F),
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
  };
  const Utility::FlexLayoutResult content =
      Utility::Row::calculate(content_bounds, content_items);
  const UI::Rect activity_bounds = content.items[0];
  const UI::Rect sidebar_bounds = content.items[1];
  const UI::Rect editor_workspace_bounds = content.items[2];

  // The tab strip fills the titlebar edge-to-edge. This keeps the Ghostty-
  // style separators flush with the chrome instead of leaving top/bottom
  // margins around the buffer labels.
  const float integrated_tab_y = 0.0F;
  const float integrated_tab_height = safe_top;
  // Keep it directly beside the logo/hamburger even while the Explorer
  // sidebar is open below it.
  const float integrated_tab_x = std::min(
      StudioEditorMetrics::titlebar_navigation_width * safe_scale, safe_width);
  const float integrated_tab_right = std::max(
      safe_width -
          StudioEditorMetrics::titlebar_window_controls_width * safe_scale,
      integrated_tab_x);
  const UI::Rect tab_bounds{
      integrated_tab_x,
      integrated_tab_y,
      integrated_tab_right - integrated_tab_x,
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

  const std::array editor_items{
      Utility::FlexItem::fixed(gutter_width),
      Utility::FlexItem::flexible(),
  };
  const Utility::FlexLayoutResult editor =
      Utility::Row::calculate(editor_row_bounds, editor_items);

  const float terminal_header_height =
      std::min(28.0F * safe_scale, terminal_bounds.height);
  const std::array terminal_items{
      Utility::FlexItem::fixed(terminal_header_height),
      Utility::FlexItem::flexible(),
  };
  const Utility::FlexLayoutResult terminal =
      Utility::Column::calculate(terminal_bounds, terminal_items);

  StudioEditorLayoutResult result;
  result.dpi_scale = safe_scale;
  result.workspace_bounds = root_bounds;
  result.tab_bar_bounds = tab_bounds;
  result.activity_bar_bounds = activity_bounds;
  result.tool_sidebar_bounds = sidebar_bounds;
  result.gutter_bounds = editor.items[0];
  result.editor_bounds = editor.items[1];
  result.terminal_panel_bounds = terminal_bounds;
  result.terminal_header_bounds = terminal.items[0];
  result.terminal_content_bounds = terminal.items[1];
  result.status_bar_bounds = status_bounds;
  const float minimap_width = std::min(
      112.0F * safe_scale, std::max(result.editor_bounds.width * 0.17F, 0.0F));
  result.minimap_bounds = {
      std::max(safe_width - scrollbar_width - minimap_width,
               result.editor_bounds.x),
      result.editor_bounds.y,
      std::min(minimap_width,
               std::max(safe_width - scrollbar_width - result.editor_bounds.x,
                        0.0F)),
      result.editor_bounds.height,
  };
  result.scrollbar_bounds = {
      std::max(safe_width - scrollbar_width, result.editor_bounds.x),
      result.editor_bounds.y,
      std::min(scrollbar_width, result.editor_bounds.width),
      result.editor_bounds.height,
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
    std::array<EditorToken, maximum_editor_tokens> &output) noexcept {
  std::size_t token_count = 0;
  std::size_t cursor = 0;

  std::size_t first_non_ws = line.find_first_not_of(" \t");
  if (first_non_ws != std::string_view::npos) {
    std::string_view trimmed = line.substr(first_non_ws);
    if (trimmed.starts_with("/**") || trimmed.starts_with("/*") ||
        trimmed.starts_with("*/") || trimmed.starts_with("* ") ||
        trimmed == "*" || trimmed.starts_with("**/")) {
      if (!output.empty()) {
        output[0] = EditorToken{line, EditorTokenKind::Comment};
        return 1;
      }
    }
  }
  const auto append = [&output, &token_count](std::string_view text,
                                              EditorTokenKind kind) {
    if (!text.empty() && token_count < output.size()) {
      output[token_count++] = EditorToken{text, kind};
    }
  };

  while (cursor < line.size() && token_count < output.size()) {
    const std::size_t token_start = cursor;
    const char character = line[cursor];
    if (character == '#') {
      append(line.substr(cursor), EditorTokenKind::Keyword);
      break;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      while (cursor < line.size() &&
             std::isspace(static_cast<unsigned char>(line[cursor])) != 0) {
        ++cursor;
      }
      append(line.substr(token_start, cursor - token_start),
             EditorTokenKind::Plain);
      continue;
    }
    if (character == '/' && cursor + 1 < line.size() &&
        line[cursor + 1] == '/') {
      append(line.substr(cursor), EditorTokenKind::Comment);
      break;
    }
    if (character == '/' && cursor + 1 < line.size() &&
        line[cursor + 1] == '*') {
      cursor += 2;
      while (cursor + 1 < line.size() &&
             !(line[cursor] == '*' && line[cursor + 1] == '/')) {
        ++cursor;
      }
      cursor = std::min(cursor + 2, line.size());
      append(line.substr(token_start, cursor - token_start),
             EditorTokenKind::Comment);
      continue;
    }
    if (character == '"' || character == '\'') {
      const char quote = character;
      ++cursor;
      while (cursor < line.size()) {
        if (line[cursor] == '\\' && cursor + 1 < line.size()) {
          cursor += 2;
          continue;
        }
        if (line[cursor] == quote) {
          ++cursor;
          break;
        }
        ++cursor;
      }
      append(line.substr(token_start, cursor - token_start),
             EditorTokenKind::String);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(character)) != 0 ||
        (character == '-' && cursor + 1 < line.size() &&
         std::isdigit(static_cast<unsigned char>(line[cursor + 1])) != 0)) {
      ++cursor;
      while (cursor < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[cursor])) != 0) {
        ++cursor;
      }
      append(line.substr(token_start, cursor - token_start),
             EditorTokenKind::Number);
      continue;
    }
    if (is_identifier_start(character)) {
      ++cursor;
      while (cursor < line.size() && is_identifier_character(line[cursor])) {
        ++cursor;
      }
      const std::string_view identifier =
          line.substr(token_start, cursor - token_start);
      EditorTokenKind kind = EditorTokenKind::Plain;
      if (cursor < line.size() && line[cursor] == ':') {
        ++cursor;
        append(line.substr(token_start, cursor - token_start),
               EditorTokenKind::Label);
        continue;
      }
      if (contains(keywords, identifier)) {
        kind = EditorTokenKind::Keyword;
      } else if (contains(types, identifier)) {
        kind = EditorTokenKind::Type;
      } else if ((identifier.front() == 'd' || identifier.front() == 'r') &&
                 identifier.size() > 1 &&
                 std::isdigit(static_cast<unsigned char>(identifier[1])) != 0) {
        kind = EditorTokenKind::Number;
      }
      append(identifier, kind);
      continue;
    }

    ++cursor;
    append(line.substr(token_start, 1), EditorTokenKind::Plain);
  }
  return token_count;
}

bool supports_editor_syntax_highlighting(std::string_view file_name) noexcept {
  std::string normalized_name{file_name};
  std::transform(normalized_name.begin(), normalized_name.end(),
                 normalized_name.begin(), [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });

  if (normalized_name == "cmakelists.txt") {
    return false;
  }

  const std::size_t dot = normalized_name.find_last_of('.');
  if (dot == std::string::npos) {
    return false;
  }
  const std::string_view extension{normalized_name.data() + dot,
                                   normalized_name.size() - dot};
  constexpr std::array<std::string_view, 14> highlighted_extensions{
      ".c",   ".cc",  ".cpp", ".cxx", ".h",    ".hh",   ".hpp",
      ".hxx", ".inl", ".m",   ".mm",  ".vert", ".frag", ".glsl",
  };
  return contains(highlighted_extensions, extension);
}

} // namespace Zenvra::UI::Editor
