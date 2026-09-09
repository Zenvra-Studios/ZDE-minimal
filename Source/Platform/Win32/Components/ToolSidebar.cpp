#include "Platform/Win32/Components/ToolSidebar.h"
#include "Platform/Win32/Components/StudioWorkspaceRenderer.h"
#include "Plugins/PluginManager.h"
#include "UI/Editor/FileIconModel.h"
#include "Utility/Fonts.h"
#include "Utility/MathUtil.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Zenvra::Platform::Win32::Components {

namespace {

using Zenvra::Utility::round_to_int;

std::string ellipsize(HDC device_context, AntialiasedFont &font,
                      std::string text, int maximum_width) {
  if (maximum_width <= 0) {
    return {};
  }
  if (font.getTextWidth(device_context, text) <= maximum_width) {
    return text;
  }
  constexpr std::string_view suffix = "...";
  if (font.getTextWidth(device_context, std::string{suffix}) > maximum_width) {
    return {};
  }
  while (!text.empty() &&
         font.getTextWidth(device_context, text + std::string{suffix}) >
             maximum_width) {
    text.pop_back();
    while (!text.empty() &&
           (static_cast<unsigned char>(text.back()) & 0xC0U) == 0x80U) {
      text.pop_back();
    }
    if (!text.empty() && static_cast<unsigned char>(text.back()) >= 0x80U) {
      text.pop_back();
    }
  }
  return text + std::string{suffix};
}

std::string to_lower(std::string_view s) {
  std::string res(s);
  std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return res;
}

std::string extension_icon_for(std::string_view id, std::string_view name) {
  const std::string lower_id = to_lower(id);
  const std::string lower_name = to_lower(name);

  if (lower_id.find("cpp") != std::string::npos || lower_name.find("clangd") != std::string::npos) {
    return "material-icon-theme/cpp.svg";
  }
  if (lower_id.find("cmake") != std::string::npos || lower_name.find("cmake") != std::string::npos) {
    return "material-icon-theme/cmake.svg";
  }
  if (lower_id.find("go") != std::string::npos || lower_name.find("go") != std::string::npos) {
    return "material-icon-theme/go.svg";
  }
  if (lower_id.find("python") != std::string::npos || lower_name.find("py") != std::string::npos) {
    return "material-icon-theme/python.svg";
  }
  if (lower_id.find("ruby") != std::string::npos || lower_name.find("ruby") != std::string::npos) {
    return "material-icon-theme/ruby.svg";
  }
  if (lower_id.find("rust") != std::string::npos || lower_name.find("rust") != std::string::npos) {
    return "material-icon-theme/rust.svg";
  }
  if (lower_id.find("markdown") != std::string::npos || lower_name.find("markdown") != std::string::npos) {
    return "material-icon-theme/markdownlint.svg";
  }
  if (lower_id.find("history") != std::string::npos || lower_name.find("history") != std::string::npos) {
    return "material-icon-theme/git.svg";
  }
  if (lower_id.find("git") != std::string::npos || lower_name.find("git") != std::string::npos) {
    return "Assets/icons/git-branch.svg";
  }
  if (lower_id.find("docker") != std::string::npos || lower_name.find("docker") != std::string::npos) {
    return "material-icon-theme/docker.svg";
  }
  return "Assets/icons/puzzle.svg";
}

} // namespace

bool ToolSidebar::initialize() {
  m_hovered_row.reset();
  m_hovered_search_row.reset();
  m_hovered_icon.reset();
  m_hovered_scrollbar = false;
  m_hovered_search_scrollbar = false;
  m_hovered_extensions_scrollbar = false;
  m_hovered_ext_installed_idx.reset();
  m_hovered_ext_recommended_idx.reset();
  m_hovered_ext_gear_idx.reset();
  m_hovered_ext_install_idx.reset();
  return m_model.initialize();
}

bool ToolSidebar::set_workspace_root(const std::filesystem::path &root) {
  m_hovered_row.reset();
  m_hovered_search_row.reset();
  m_hovered_scrollbar = false;
  m_hovered_search_scrollbar = false;
  m_hovered_extensions_scrollbar = false;
  m_hovered_ext_installed_idx.reset();
  m_hovered_ext_recommended_idx.reset();
  m_hovered_ext_gear_idx.reset();
  m_hovered_ext_install_idx.reset();
  m_search_model.set_workspace_root(root);
  return m_model.initialize(root);
}

void ToolSidebar::clear_workspace() noexcept {
  m_hovered_row.reset();
  m_hovered_search_row.reset();
  m_hovered_scrollbar = false;
  m_hovered_search_scrollbar = false;
  m_hovered_extensions_scrollbar = false;
  m_hovered_ext_installed_idx.reset();
  m_hovered_ext_recommended_idx.reset();
  m_hovered_ext_gear_idx.reset();
  m_hovered_ext_install_idx.reset();
  m_search_model.clear_results();
  m_model.clear_workspace();
}

bool ToolSidebar::activate(UI::Editor::SidebarIcon icon) noexcept {
  m_hovered_row.reset();
  m_hovered_search_row.reset();
  m_hovered_scrollbar = false;
  m_hovered_search_scrollbar = false;
  m_hovered_extensions_scrollbar = false;
  if (icon == UI::Editor::SidebarIcon::Search) {
    m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Search);
    if (!m_search_model.get_search_query().empty()) {
      m_search_model.execute_search();
    }
  } else if (icon == UI::Editor::SidebarIcon::Services) {
    m_ext_search_focused = true;
  }
  return m_model.activate(icon);
}

bool ToolSidebar::handle_char(char32_t codepoint) {
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
    if (codepoint < 0x20 && codepoint != 0x09) {
      return false;
    }
    m_search_model.insert_char(codepoint);
    return true;
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services && m_ext_search_focused) {
    if (codepoint >= 0x20 && codepoint <= 0x7E) {
      m_ext_search_query.push_back(static_cast<char>(codepoint));
      m_ext_scroll_y = 0.0F;
      return true;
    }
  }
  return false;
}

bool ToolSidebar::handle_key(int vkey, bool ctrl, bool shift, bool alt) {
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services && m_ext_search_focused) {
    if (vkey == VK_BACK) {
      if (!m_ext_search_query.empty()) {
        m_ext_search_query.pop_back();
        m_ext_scroll_y = 0.0F;
      }
      return true;
    }
    if (vkey == VK_ESCAPE) {
      if (!m_ext_search_query.empty()) {
        m_ext_search_query.clear();
        m_ext_scroll_y = 0.0F;
      } else {
        m_ext_search_focused = false;
      }
      return true;
    }
    return false;
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    // Ctrl+C (Copy File/Folder or Multi-Selection)
    if (ctrl && !shift && !alt && (vkey == 'C' || vkey == 'c')) {
      const auto& selected = m_model.get_selected_paths();
      if (!selected.empty()) {
        m_model.copy_selected_to_clipboard();
        std::wstring all_paths_w;
        for (const auto& p : selected) {
          if (!all_paths_w.empty()) all_paths_w += L"\r\n";
          all_paths_w += p.wstring();
        }
        if (OpenClipboard(nullptr)) {
          EmptyClipboard();
          const size_t bytes = (all_paths_w.length() + 1) * sizeof(wchar_t);
          HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
          if (hMem) {
            memcpy(GlobalLock(hMem), all_paths_w.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
          }
          CloseClipboard();
        }
        return true;
      }
      return false;
    }

    // Ctrl+X (Cut File/Folder or Multi-Selection)
    if (ctrl && !shift && !alt && (vkey == 'X' || vkey == 'x')) {
      const auto& selected = m_model.get_selected_paths();
      if (!selected.empty()) {
        m_model.cut_selected_to_clipboard();
        std::wstring all_paths_w;
        for (const auto& p : selected) {
          if (!all_paths_w.empty()) all_paths_w += L"\r\n";
          all_paths_w += p.wstring();
        }
        if (OpenClipboard(nullptr)) {
          EmptyClipboard();
          const size_t bytes = (all_paths_w.length() + 1) * sizeof(wchar_t);
          HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
          if (hMem) {
            memcpy(GlobalLock(hMem), all_paths_w.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
          }
          CloseClipboard();
        }
        return true;
      }
      return false;
    }

    // Ctrl+V (Paste File/Folder)
    if (ctrl && !shift && !alt && (vkey == 'V' || vkey == 'v')) {
      std::filesystem::path out_p;
      if (m_model.paste_from_clipboard(out_p)) {
        return true;
      }
      return false;
    }

    // Arrow Key Navigation & Activation
    if (!ctrl && !alt && !shift) {
      if (vkey == VK_UP) {
        return m_model.select_previous();
      }
      if (vkey == VK_DOWN) {
        return m_model.select_next();
      }
      if (vkey == VK_LEFT) {
        return m_model.select_parent_or_collapse();
      }
      if (vkey == VK_RIGHT) {
        return m_model.select_first_child_or_expand();
      }
      if (vkey == VK_RETURN || vkey == VK_SPACE) {
        const auto action = m_model.activate_selected();
        return action.handled;
      }
      if (vkey == VK_DELETE) {
        if (!m_model.get_selected_paths().empty()) {
          return m_model.delete_selected_items();
        }
      }
    }
    return false;
  }

  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Search) {
    return false;
  }

  // Ctrl+A (Select All)
  if (ctrl && (vkey == 'A' || vkey == 'a')) {
    m_search_model.select_all();
    return true;
  }

  // Ctrl+C (Copy Selection)
  if (ctrl && (vkey == 'C' || vkey == 'c')) {
    const std::string sel = m_search_model.get_selected_text();
    if (!sel.empty() && OpenClipboard(nullptr)) {
      EmptyClipboard();
      const int wsize = MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), -1, NULL, 0);
      if (wsize > 0) {
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wsize) * sizeof(wchar_t));
        if (hMem) {
          wchar_t *pMem = static_cast<wchar_t *>(GlobalLock(hMem));
          if (pMem) {
            MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), -1, pMem, wsize);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
          }
        }
      }
      CloseClipboard();
    }
    return true;
  }

  // Ctrl+X (Cut Selection)
  if (ctrl && (vkey == 'X' || vkey == 'x')) {
    const std::string sel = m_search_model.get_selected_text();
    if (!sel.empty()) {
      if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        const int wsize = MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), -1, NULL, 0);
        if (wsize > 0) {
          HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(wsize) * sizeof(wchar_t));
          if (hMem) {
            wchar_t *pMem = static_cast<wchar_t *>(GlobalLock(hMem));
            if (pMem) {
              MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), -1, pMem, wsize);
              GlobalUnlock(hMem);
              SetClipboardData(CF_UNICODETEXT, hMem);
            }
          }
        }
        CloseClipboard();
      }
      m_search_model.handle_delete();
    }
    return true;
  }

  // Ctrl+V (Paste)
  if (ctrl && (vkey == 'V' || vkey == 'v')) {
    if (OpenClipboard(nullptr)) {
      HANDLE hData = GetClipboardData(CF_UNICODETEXT);
      if (hData) {
        const wchar_t *pText = static_cast<const wchar_t *>(GlobalLock(hData));
        if (pText) {
          int size_needed = WideCharToMultiByte(CP_UTF8, 0, pText, -1, NULL, 0, NULL, NULL);
          if (size_needed > 1) {
            std::string utf8_str(size_needed - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, pText, -1, &utf8_str[0], size_needed, NULL, NULL);
            m_search_model.insert_text(utf8_str);
          }
          GlobalUnlock(hData);
        }
      }
      CloseClipboard();
    }
    return true;
  }

  if (vkey == VK_BACK) {
    m_search_model.handle_backspace();
    return true;
  }
  if (vkey == VK_DELETE) {
    m_search_model.handle_delete();
    return true;
  }
  if (vkey == VK_LEFT) {
    m_search_model.handle_left(shift);
    return true;
  }
  if (vkey == VK_RIGHT) {
    m_search_model.handle_right(shift);
    return true;
  }
  if (vkey == VK_HOME) {
    m_search_model.handle_home(shift);
    return true;
  }
  if (vkey == VK_END) {
    m_search_model.handle_end(shift);
    return true;
  }
  if (vkey == VK_ESCAPE) {
    if (m_search_model.has_selection()) {
      m_search_model.clear_selection();
    } else {
      m_search_model.clear_query();
    }
    return true;
  }
  if (vkey == VK_RETURN) {
    if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace) {
      if (ctrl || alt) {
        m_search_model.replace_all();
      }
    } else {
      m_search_model.execute_search();
    }
    return true;
  }
  if (vkey == VK_TAB) {
    if (m_search_model.is_replace_expanded()) {
      if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search) {
        m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Replace);
      } else {
        m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Search);
      }
      return true;
    }
  }

  return false;
}

bool ToolSidebar::is_search_focused() const noexcept {
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    return m_ext_search_focused;
  }
  return m_model.get_active_icon() == UI::Editor::SidebarIcon::Search &&
         m_search_model.get_focused_input() != UI::Editor::SearchInputFocus::None;
}

float ToolSidebar::search_tree_top_y(const UI::Editor::StudioEditorLayoutResult& layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float header_h = header_height * scale;
  const float input_h = 28.0F * scale;
  const float replace_h = m_search_model.is_replace_expanded() ? 32.0F * scale : 0.0F;
  const float summary_h = 24.0F * scale;
  return layout.tool_sidebar_bounds.y + header_h + 8.0F * scale + input_h + replace_h + summary_h;
}

std::size_t ToolSidebar::search_viewport_row_count(const UI::Editor::StudioEditorLayoutResult& layout) const noexcept {
  const float top = search_tree_top_y(layout);
  const float available = layout.tool_sidebar_bounds.bottom() - top;
  if (available <= 0.0F) return 0;
  return static_cast<std::size_t>(available / (row_height * layout.dpi_scale));
}

std::optional<std::size_t> ToolSidebar::search_row_from_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_y) const noexcept {
  const float top = search_tree_top_y(layout);
  if (point_y < top || point_y >= layout.tool_sidebar_bounds.bottom()) {
    return std::nullopt;
  }
  const float scale = layout.dpi_scale;
  const auto idx = static_cast<std::size_t>((point_y - top) / (row_height * scale));
  const auto visible_rows = m_search_model.get_visible_rows();
  const std::size_t actual_idx = m_search_model.get_scroll_offset() + idx;
  if (actual_idx < visible_rows.size()) {
    return actual_idx;
  }
  return std::nullopt;
}

UI::Rect ToolSidebar::search_scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float top = search_tree_top_y(layout);
  const float width = 8.0F * scale;
  return UI::Rect{
      layout.tool_sidebar_bounds.right() - width,
      top,
      width,
      std::max(layout.tool_sidebar_bounds.bottom() - top, 0.0F)
  };
}

UI::Rect ToolSidebar::extensions_scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult& layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float search_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
  const float search_height = 26.0F * scale;
  const float top = search_top + search_height + 8.0F * scale;
  const float width = 8.0F * scale;
  return UI::Rect{
      layout.tool_sidebar_bounds.right() - width,
      top,
      width,
      std::max(layout.tool_sidebar_bounds.bottom() - top, 0.0F)
  };
}

SidebarPressResult ToolSidebar::handle_search_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float top = panel.y;

  // Header Actions
  const float header_center_y = top + header_height * 0.5F * scale;
  const float btn_size = 20.0F * scale;

  const UI::Rect collapse_btn{panel.right() - 24.0F * scale - btn_size * 2.0F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (collapse_btn.contains(point_x, point_y)) {
    m_search_model.collapse_all();
    return SidebarPressResult{.handled = true};
  }

  const UI::Rect clear_btn{panel.right() - 22.0F * scale - btn_size, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (clear_btn.contains(point_x, point_y)) {
    m_search_model.clear_query();
    return SidebarPressResult{.handled = true};
  }

  const UI::Rect refresh_btn{panel.right() - 20.0F * scale, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (refresh_btn.contains(point_x, point_y)) {
    m_search_model.execute_search();
    return SidebarPressResult{.handled = true};
  }

  // Replace Chevron Expand/Collapse Toggle
  const float input_top = top + header_height * scale + 8.0F * scale;
  const UI::Rect chevron_btn{panel.x + 8.0F * scale, input_top + 4.0F * scale, 16.0F * scale, 20.0F * scale};
  if (chevron_btn.contains(point_x, point_y)) {
    m_search_model.toggle_replace_expanded();
    return SidebarPressResult{.handled = true};
  }

  // Search Input Box & Toggles
  const UI::Rect search_bounds{
      panel.x + 28.0F * scale,
      input_top,
      std::max(panel.width - 36.0F * scale, 0.0F),
      26.0F * scale
  };

  const float opt_btn_w = 20.0F * scale;
  const float opt_btn_h = 20.0F * scale;
  const float opt_btn_y = input_top + 3.0F * scale;

  const UI::Rect regex_btn{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (regex_btn.contains(point_x, point_y)) {
    m_search_model.toggle_use_regex();
    return SidebarPressResult{.handled = true};
  }

  const UI::Rect word_btn{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (word_btn.contains(point_x, point_y)) {
    m_search_model.toggle_match_word();
    return SidebarPressResult{.handled = true};
  }

  const UI::Rect case_btn{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (case_btn.contains(point_x, point_y)) {
    m_search_model.toggle_match_case();
    return SidebarPressResult{.handled = true};
  }

  if (search_bounds.contains(point_x, point_y)) {
    m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Search);
    const std::uint64_t now_tick = GetTickCount64();
    if (now_tick - m_last_search_click_time < 350) {
      m_search_model.select_all();
    } else {
      const std::string_view q = m_search_model.get_search_query();
      const float text_x = search_bounds.x + 6.0F * scale;
      const float char_w = 7.2F * scale;
      std::size_t idx = 0;
      if (point_x > text_x) {
        idx = static_cast<std::size_t>((point_x - text_x + char_w * 0.5F) / char_w);
        idx = std::min(idx, q.size());
      }
      m_search_model.set_caret_and_selection(idx, idx, idx);
      m_is_selecting_search_text = true;
    }
    m_last_search_click_time = now_tick;
    return SidebarPressResult{.handled = true};
  }

  // Replace Input Box & Toggles
  if (m_search_model.is_replace_expanded()) {
    const float replace_top = input_top + 30.0F * scale;
    const UI::Rect replace_bounds{
        panel.x + 28.0F * scale,
        replace_top,
        std::max(panel.width - 64.0F * scale, 0.0F),
        26.0F * scale
    };

    const UI::Rect preserve_case_btn{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h};
    if (preserve_case_btn.contains(point_x, point_y)) {
      m_search_model.toggle_preserve_case();
      return SidebarPressResult{.handled = true};
    }

    const UI::Rect replace_all_btn{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale};
    if (replace_all_btn.contains(point_x, point_y)) {
      m_search_model.replace_all();
      return SidebarPressResult{.handled = true};
    }

    if (replace_bounds.contains(point_x, point_y)) {
      m_search_model.set_focused_input(UI::Editor::SearchInputFocus::Replace);
      const std::uint64_t now_tick = GetTickCount64();
      if (now_tick - m_last_search_click_time < 350) {
        m_search_model.select_all();
      } else {
        const std::string_view q = m_search_model.get_replace_query();
        const float text_x = replace_bounds.x + 6.0F * scale;
        const float char_w = 7.2F * scale;
        std::size_t idx = 0;
        if (point_x > text_x) {
          idx = static_cast<std::size_t>((point_x - text_x + char_w * 0.5F) / char_w);
          idx = std::min(idx, q.size());
        }
        m_search_model.set_caret_and_selection(idx, idx, idx);
        m_is_selecting_search_text = true;
      }
      m_last_search_click_time = now_tick;
      return SidebarPressResult{.handled = true};
    }
  }

  // Search scrollbar press & drag
  const auto visible_rows = m_search_model.get_visible_rows();
  const std::size_t search_rows = search_viewport_row_count(layout);
  m_search_scrollbar.set_track_bounds(search_scrollbar_bounds(layout));
  m_search_scrollbar.set_metrics(visible_rows.size(), search_rows);
  static_cast<void>(m_search_scrollbar.scroll_to(m_search_model.get_scroll_offset()));
  if (m_search_scrollbar.handle_pointer_press(point_x, point_y)) {
    m_search_model.set_scroll_offset(m_search_scrollbar.get_scroll_offset());
    return SidebarPressResult{.handled = true};
  }

  // Results Tree Row Activation
  const auto row = search_row_from_point(layout, point_y);
  if (row) {
    const auto nav = m_search_model.activate_visible_row(*row);
    if (nav) {
      return SidebarPressResult{
          .handled = true,
          .action = SidebarActionKind::OpenFile,
          .path = nav->path,
          .line = nav->line,
          .column = nav->column
      };
    }
    return SidebarPressResult{.handled = true};
  }

  return SidebarPressResult{.handled = true};
}

bool ToolSidebar::handle_search_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float top = panel.y;

  const float header_center_y = top + header_height * 0.5F * scale;
  const float btn_size = 20.0F * scale;

  m_hover_search_collapse_all = UI::Rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y);
  m_hover_search_clear = UI::Rect{panel.right() - 22.0F * scale - btn_size, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y);
  m_hover_search_refresh = UI::Rect{panel.right() - 20.0F * scale, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y);

  const float input_top = top + header_height * scale + 8.0F * scale;
  m_hover_search_chevron = UI::Rect{panel.x + 8.0F * scale, input_top + 4.0F * scale, 16.0F * scale, 20.0F * scale}.contains(point_x, point_y);

  const UI::Rect search_bounds{panel.x + 28.0F * scale, input_top, std::max(panel.width - 36.0F * scale, 0.0F), 26.0F * scale};
  const float opt_btn_w = 20.0F * scale;
  const float opt_btn_h = 20.0F * scale;
  const float opt_btn_y = input_top + 3.0F * scale;

  m_hover_search_use_regex = UI::Rect{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y);
  m_hover_search_match_word = UI::Rect{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y);
  m_hover_search_match_case = UI::Rect{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y);

  if (m_search_model.is_replace_expanded()) {
    const float replace_top = input_top + 30.0F * scale;
    const UI::Rect replace_bounds{panel.x + 28.0F * scale, replace_top, std::max(panel.width - 64.0F * scale, 0.0F), 26.0F * scale};
    m_hover_search_preserve_case = UI::Rect{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h}.contains(point_x, point_y);
    m_hover_search_replace_all = UI::Rect{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale}.contains(point_x, point_y);
  } else {
    m_hover_search_preserve_case = false;
    m_hover_search_replace_all = false;
  }

  const auto prev_search_row = m_hovered_search_row;
  m_hovered_search_row = search_row_from_point(layout, point_y);
  m_hovered_search_scrollbar = search_scrollbar_bounds(layout).contains(point_x, point_y) &&
                               m_search_model.get_visible_rows().size() > search_viewport_row_count(layout);

  return prev_search_row != m_hovered_search_row;
}

bool ToolSidebar::handle_extensions_scroll(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::ptrdiff_t line_delta) noexcept {
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float search_top = panel.y + header_height * scale + 8.0F * scale;
  const float search_height = 26.0F * scale;
  const float content_top = search_top + search_height + 8.0F * scale;
  const float viewport_h = std::max(panel.bottom() - content_top, 0.0F);

  auto& pm = Zenvra::Plugins::PluginManager::instance();
  std::size_t inst_count = pm.get_all_plugins().size();
  std::size_t rec_count = pm.get_marketplace().get_all_entries().size();

  if (!m_ext_search_query.empty()) {
    const std::string q = to_lower(m_ext_search_query);
    inst_count = 0;
    for (const auto& p : pm.get_all_plugins()) {
      if (to_lower(p->get_name()).find(q) != std::string::npos ||
          to_lower(p->get_id()).find(q) != std::string::npos ||
          to_lower(p->get_description()).find(q) != std::string::npos ||
          to_lower(p->get_manifest().get_publisher()).find(q) != std::string::npos) {
        ++inst_count;
      }
    }
    rec_count = 0;
    for (const auto& e : pm.get_marketplace().get_all_entries()) {
      if (pm.get_registry().has_plugin(e.id)) continue;
      if (to_lower(e.name).find(q) != std::string::npos ||
          to_lower(e.id).find(q) != std::string::npos ||
          to_lower(e.description).find(q) != std::string::npos ||
          to_lower(e.publisher).find(q) != std::string::npos) {
        ++rec_count;
      }
    }
  }

  float total_h = 24.0F * scale; // Installed header
  if (m_ext_installed_expanded) {
    total_h += static_cast<float>(inst_count) * 62.0F * scale;
  }
  total_h += 24.0F * scale; // Recommended header
  if (m_ext_recommended_expanded) {
    total_h += static_cast<float>(rec_count) * 64.0F * scale;
  }

  const float max_scroll = std::max(total_h - viewport_h, 0.0F);
  const float prev_scroll = m_ext_scroll_y;
  m_ext_scroll_y = std::clamp(m_ext_scroll_y + static_cast<float>(line_delta) * 36.0F * scale, 0.0F, max_scroll);
  return m_ext_scroll_y != prev_scroll;
}

bool ToolSidebar::handle_extensions_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float top = panel.y;

  const float header_center_y = top + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  const UI::Rect ellipsis_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};

  const bool next_refresh = refresh_rect.contains(point_x, point_y);
  const bool next_more = ellipsis_rect.contains(point_x, point_y);

  const float search_top = top + header_height * scale + 8.0F * scale;
  const float search_height = 26.0F * scale;
  const UI::Rect search_bounds{panel.x + 12.0F * scale, search_top, std::max(panel.width - 24.0F * scale, 0.0F), search_height};
  const UI::Rect clear_rect{search_bounds.right() - 36.0F * scale, search_top + 3.0F * scale, 18.0F * scale, 20.0F * scale};
  const UI::Rect filter_rect{search_bounds.right() - 18.0F * scale, search_top + 3.0F * scale, 18.0F * scale, 20.0F * scale};

  const bool next_clear = !m_ext_search_query.empty() && clear_rect.contains(point_x, point_y);
  const bool next_filter = filter_rect.contains(point_x, point_y);

  const bool next_scrollbar = extensions_scrollbar_bounds(layout).contains(point_x, point_y);

  const float content_top = search_top + search_height + 8.0F * scale;

  std::optional<std::size_t> next_inst_idx;
  std::optional<std::size_t> next_rec_idx;
  std::optional<std::size_t> next_gear_idx;
  std::optional<std::size_t> next_install_idx;
  std::optional<std::size_t> next_uninstall_idx;
  bool next_inst_hdr = false;
  bool next_rec_hdr = false;

  if (point_y >= content_top && point_y <= panel.bottom() && point_x >= panel.x && point_x <= panel.right()) {
    float cur_y = content_top - m_ext_scroll_y;
    const float header_h = 24.0F * scale;

    auto& pm = Zenvra::Plugins::PluginManager::instance();
    std::vector<std::shared_ptr<Zenvra::Plugins::Plugin>> matching_installed;
    for (const auto& p : pm.get_all_plugins()) {
      if (!p) continue;
      std::error_code ec;
      if (!std::filesystem::exists(p->get_install_path(), ec)) continue;
      if (!std::filesystem::exists(p->get_install_path() / "plugin.json", ec) &&
          !std::filesystem::exists(p->get_install_path() / "package.json", ec)) continue;

      if (m_ext_search_query.empty()) {
        matching_installed.push_back(p);
      } else {
        const std::string q = to_lower(m_ext_search_query);
        if (to_lower(p->get_name()).find(q) != std::string::npos ||
            to_lower(p->get_id()).find(q) != std::string::npos ||
            to_lower(p->get_description()).find(q) != std::string::npos ||
            to_lower(p->get_manifest().get_publisher()).find(q) != std::string::npos ||
            to_lower(p->get_manifest().get_category()).find(q) != std::string::npos) {
          matching_installed.push_back(p);
        }
      }
    }

    if (!matching_installed.empty() || m_ext_search_query.empty()) {
      const UI::Rect inst_hdr_rect{panel.x, cur_y, panel.width, header_h};
      if (inst_hdr_rect.contains(point_x, point_y)) {
        next_inst_hdr = true;
      }
      cur_y += header_h;

      if (m_ext_installed_expanded) {
        if (matching_installed.empty()) {
          cur_y += 28.0F * scale;
        } else {
          for (std::size_t i = 0; i < matching_installed.size(); ++i) {
            const float item_h = 62.0F * scale;
            const UI::Rect item_rect{panel.x, cur_y, panel.width, item_h};
            if (item_rect.contains(point_x, point_y)) {
              next_inst_idx = i;
              const float uninst_btn_w = 64.0F * scale;
              const float uninst_btn_h = 22.0F * scale;
              const UI::Rect uninst_btn_rect{panel.right() - uninst_btn_w - 38.0F * scale, cur_y + 18.0F * scale, uninst_btn_w, uninst_btn_h};
              if (uninst_btn_rect.contains(point_x, point_y)) {
                next_uninstall_idx = i;
              }
              const UI::Rect gear_rect{panel.right() - 32.0F * scale, cur_y + 18.0F * scale, 22.0F * scale, 22.0F * scale};
              if (gear_rect.contains(point_x, point_y)) {
                next_gear_idx = i;
              }
            }
            cur_y += item_h;
          }
        }
      }
    }

    std::vector<Zenvra::Plugins::Marketplace::MarketplacePluginEntry> matching_recommended;
    for (const auto& e : pm.get_marketplace().get_all_entries()) {
      if (pm.get_registry().has_plugin(e.id)) {
        auto p = pm.get_registry().get_plugin(e.id);
        std::error_code ec;
        if (p && std::filesystem::exists(p->get_install_path(), ec)) {
          continue;
        }
      }
      if (m_ext_search_query.empty()) {
        matching_recommended.push_back(e);
      } else {
        const std::string q = to_lower(m_ext_search_query);
        if (to_lower(e.name).find(q) != std::string::npos ||
            to_lower(e.id).find(q) != std::string::npos ||
            to_lower(e.description).find(q) != std::string::npos ||
            to_lower(e.publisher).find(q) != std::string::npos ||
            to_lower(e.category).find(q) != std::string::npos) {
          matching_recommended.push_back(e);
        }
      }
    }

    if (!matching_recommended.empty() || m_ext_search_query.empty()) {
      const UI::Rect rec_hdr_rect{panel.x, cur_y, panel.width, header_h};
      if (rec_hdr_rect.contains(point_x, point_y)) {
        next_rec_hdr = true;
      }
      cur_y += header_h;

      if (m_ext_recommended_expanded) {
        for (std::size_t j = 0; j < matching_recommended.size(); ++j) {
          const float item_h = 64.0F * scale;
          const UI::Rect item_rect{panel.x, cur_y, panel.width, item_h};
          if (item_rect.contains(point_x, point_y)) {
            next_rec_idx = j;
            const float btn_w = 56.0F * scale;
            const float btn_h = 22.0F * scale;
            const UI::Rect btn_rect{panel.right() - btn_w - 12.0F * scale, cur_y + 36.0F * scale, btn_w, btn_h};
            if (btn_rect.contains(point_x, point_y)) {
              next_install_idx = j;
            }
          }
          cur_y += item_h;
        }
      }
    }
  }

  const bool changed = (next_refresh != m_hover_ext_refresh) ||
                       (next_more != m_hover_ext_more) ||
                       (next_clear != m_hover_ext_clear) ||
                       (next_filter != m_hover_ext_filter) ||
                       (next_scrollbar != m_hovered_extensions_scrollbar) ||
                       (next_inst_hdr != m_hover_ext_installed_header) ||
                       (next_rec_hdr != m_hover_ext_recommended_header) ||
                       (next_inst_idx != m_hovered_ext_installed_idx) ||
                       (next_rec_idx != m_hovered_ext_recommended_idx) ||
                       (next_gear_idx != m_hovered_ext_gear_idx) ||
                       (next_install_idx != m_hovered_ext_install_idx) ||
                       (next_uninstall_idx != m_hovered_ext_uninstall_idx);

  m_hover_ext_refresh = next_refresh;
  m_hover_ext_more = next_more;
  m_hover_ext_clear = next_clear;
  m_hover_ext_filter = next_filter;
  m_hovered_extensions_scrollbar = next_scrollbar;
  m_hover_ext_installed_header = next_inst_hdr;
  m_hover_ext_recommended_header = next_rec_hdr;
  m_hovered_ext_installed_idx = next_inst_idx;
  m_hovered_ext_recommended_idx = next_rec_idx;
  m_hovered_ext_gear_idx = next_gear_idx;
  m_hovered_ext_install_idx = next_install_idx;
  m_hovered_ext_uninstall_idx = next_uninstall_idx;

  return changed;
}

SidebarPressResult ToolSidebar::handle_extensions_press(
    const UI::Editor::StudioEditorLayoutResult &layout,
    float point_x, float point_y) {
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float top = panel.y;

  const float header_center_y = top + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  // Refresh
  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (refresh_rect.contains(point_x, point_y)) {
    Zenvra::Plugins::PluginManager::instance().scan_plugins();
    return SidebarPressResult{.handled = true};
  }

  // More options
  const UI::Rect ellipsis_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (ellipsis_rect.contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true};
  }

  // Search Box
  const float search_top = top + header_height * scale + 8.0F * scale;
  const float search_height = 26.0F * scale;
  const UI::Rect search_bounds{panel.x + 12.0F * scale, search_top, std::max(panel.width - 24.0F * scale, 0.0F), search_height};
  const UI::Rect clear_rect{search_bounds.right() - 36.0F * scale, search_top + 3.0F * scale, 18.0F * scale, 20.0F * scale};

  if (!m_ext_search_query.empty() && clear_rect.contains(point_x, point_y)) {
    m_ext_search_query.clear();
    m_ext_scroll_y = 0.0F;
    return SidebarPressResult{.handled = true};
  }

  if (search_bounds.contains(point_x, point_y)) {
    m_ext_search_focused = true;
    return SidebarPressResult{.handled = true};
  }

  // Clicking outside search
  m_ext_search_focused = false;

  // Scrollbar press
  if (m_extensions_scrollbar.handle_pointer_press(point_x, point_y)) {
    m_ext_scroll_y = static_cast<float>(m_extensions_scrollbar.get_scroll_offset());
    return SidebarPressResult{.handled = true};
  }

  // Content interactions
  const float content_top = search_top + search_height + 8.0F * scale;

  if (point_y >= content_top && point_y <= panel.bottom() && point_x >= panel.x && point_x <= panel.right()) {
    float cur_y = content_top - m_ext_scroll_y;
    const float header_h = 24.0F * scale;

    auto& pm = Zenvra::Plugins::PluginManager::instance();
    std::vector<std::shared_ptr<Zenvra::Plugins::Plugin>> matching_installed;
    for (const auto& p : pm.get_all_plugins()) {
      if (!p) continue;
      std::error_code ec;
      if (!std::filesystem::exists(p->get_install_path(), ec)) continue;
      if (!std::filesystem::exists(p->get_install_path() / "plugin.json", ec) &&
          !std::filesystem::exists(p->get_install_path() / "package.json", ec)) continue;

      if (m_ext_search_query.empty()) {
        matching_installed.push_back(p);
      } else {
        const std::string q = to_lower(m_ext_search_query);
        if (to_lower(p->get_name()).find(q) != std::string::npos ||
            to_lower(p->get_id()).find(q) != std::string::npos ||
            to_lower(p->get_description()).find(q) != std::string::npos ||
            to_lower(p->get_manifest().get_publisher()).find(q) != std::string::npos ||
            to_lower(p->get_manifest().get_category()).find(q) != std::string::npos) {
          matching_installed.push_back(p);
        }
      }
    }

    if (!matching_installed.empty() || m_ext_search_query.empty()) {
      const UI::Rect inst_hdr_rect{panel.x, cur_y, panel.width, header_h};
      if (inst_hdr_rect.contains(point_x, point_y)) {
        m_ext_installed_expanded = !m_ext_installed_expanded;
        return SidebarPressResult{.handled = true};
      }
      cur_y += header_h;

      if (m_ext_installed_expanded) {
        if (matching_installed.empty()) {
          cur_y += 28.0F * scale;
        } else {
          for (std::size_t i = 0; i < matching_installed.size(); ++i) {
            const float item_h = 62.0F * scale;
            const UI::Rect item_rect{panel.x, cur_y, panel.width, item_h};
            if (item_rect.contains(point_x, point_y)) {
              const float uninst_btn_w = 64.0F * scale;
              const float uninst_btn_h = 22.0F * scale;
              const UI::Rect uninst_btn_rect{panel.right() - uninst_btn_w - 38.0F * scale, cur_y + 18.0F * scale, uninst_btn_w, uninst_btn_h};
              if (uninst_btn_rect.contains(point_x, point_y)) {
                auto p = matching_installed[i];
                pm.uninstall_plugin(p->get_id());
                return SidebarPressResult{.handled = true};
              }
              const UI::Rect gear_rect{panel.right() - 32.0F * scale, cur_y + 18.0F * scale, 22.0F * scale, 22.0F * scale};
              if (gear_rect.contains(point_x, point_y)) {
                auto p = matching_installed[i];
                if (p->is_enabled()) {
                  pm.disable_plugin(p->get_id());
                } else {
                  pm.enable_plugin(p->get_id());
                }
                return SidebarPressResult{.handled = true};
              }
              return SidebarPressResult{.handled = true};
            }
            cur_y += item_h;
          }
        }
      }
    }

    std::vector<Zenvra::Plugins::Marketplace::MarketplacePluginEntry> matching_recommended;
    for (const auto& e : pm.get_marketplace().get_all_entries()) {
      if (pm.get_registry().has_plugin(e.id)) {
        auto p = pm.get_registry().get_plugin(e.id);
        std::error_code ec;
        if (p && std::filesystem::exists(p->get_install_path(), ec)) {
          continue;
        }
      }
      if (m_ext_search_query.empty()) {
        matching_recommended.push_back(e);
      } else {
        const std::string q = to_lower(m_ext_search_query);
        if (to_lower(e.name).find(q) != std::string::npos ||
            to_lower(e.id).find(q) != std::string::npos ||
            to_lower(e.description).find(q) != std::string::npos ||
            to_lower(e.publisher).find(q) != std::string::npos ||
            to_lower(e.category).find(q) != std::string::npos) {
          matching_recommended.push_back(e);
        }
      }
    }

    if (!matching_recommended.empty() || m_ext_search_query.empty()) {
      const UI::Rect rec_hdr_rect{panel.x, cur_y, panel.width, header_h};
      if (rec_hdr_rect.contains(point_x, point_y)) {
        m_ext_recommended_expanded = !m_ext_recommended_expanded;
        return SidebarPressResult{.handled = true};
      }
      cur_y += header_h;

      if (m_ext_recommended_expanded) {
        for (std::size_t j = 0; j < matching_recommended.size(); ++j) {
          const float item_h = 64.0F * scale;
          const UI::Rect item_rect{panel.x, cur_y, panel.width, item_h};
          if (item_rect.contains(point_x, point_y)) {
            const float btn_w = 56.0F * scale;
            const float btn_h = 22.0F * scale;
            const UI::Rect btn_rect{panel.right() - btn_w - 12.0F * scale, cur_y + 36.0F * scale, btn_w, btn_h};
            if (btn_rect.contains(point_x, point_y)) {
              // Trigger install!
              const auto& entry = matching_recommended[j];
              const auto target_path = pm.get_installer()
                  ? pm.get_installer()->get_target_path(entry.category, entry.id)
                  : (pm.get_plugins_root() / entry.category / entry.id);

              std::error_code ec;
              std::filesystem::create_directories(target_path, ec);

              Zenvra::Plugins::PluginManifest mf;
              mf.set_id(entry.id);
              mf.set_name(entry.name);
              mf.set_version(entry.version);
              mf.set_description(entry.description);
              mf.set_publisher(entry.publisher);
              mf.set_category(entry.category);

              // Physically write plugin.json so it exists on disk!
              {
                std::ofstream out(target_path / "plugin.json");
                if (out.is_open()) {
                  out << mf.to_json().dump(2);
                }
              }

              if (!entry.repository_url.empty()) {
                pm.install_from_git(entry.repository_url);
              }

              auto new_p = std::make_shared<Zenvra::Plugins::Plugin>(
                  mf, target_path,
                  entry.repository_url.empty() ? Zenvra::Plugins::PluginSource::Local : Zenvra::Plugins::PluginSource::Git);
              new_p->set_state(Zenvra::Plugins::PluginState::Installed);
              new_p->set_enabled(true);
              pm.get_registry().register_plugin(new_p);
              pm.activate_plugin(new_p);
              pm.get_registry().save_to_disk(pm.get_plugins_root() / ".registry" / "plugins.json");

              return SidebarPressResult{.handled = true};
            }
            return SidebarPressResult{.handled = true};
          }
          cur_y += item_h;
        }
      }
    }
  }

  return SidebarPressResult{.handled = true};
}

bool ToolSidebar::handle_tool_plugin_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;
  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  const UI::Rect switch_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};

  const bool next_switch = switch_rect.contains(point_x, point_y);
  const bool next_refresh = refresh_rect.contains(point_x, point_y);

  float cur_y = panel.y + header_height * scale + 12.0F * scale + 52.0F * scale + 14.0F * scale;
  const float content_left = panel.x + 12.0F * scale;
  const float content_w = std::max(panel.width - 24.0F * scale, 0.0F);

  // Jump past sections to find action buttons
  cur_y += 26.0F * scale;
  if (m_tool_section1_expanded) {
    cur_y += 34.0F * scale * 3;
  }
  cur_y += 8.0F * scale;

  cur_y += 26.0F * scale;
  if (m_tool_section2_expanded) {
    cur_y += 24.0F * scale * 3;
  }
  cur_y += 16.0F * scale;

  const UI::Rect btn1_rect{content_left, cur_y, content_w, 26.0F * scale};
  const UI::Rect btn2_rect{content_left, cur_y + 32.0F * scale, content_w, 26.0F * scale};

  const bool next_btn1 = btn1_rect.contains(point_x, point_y);
  const bool next_btn2 = btn2_rect.contains(point_x, point_y);

  const bool changed = (next_switch != m_hover_tool_switch) ||
                       (next_refresh != m_hover_tool_refresh) ||
                       (next_btn1 != m_hover_tool_action_btn1) ||
                       (next_btn2 != m_hover_tool_action_btn2);

  m_hover_tool_switch = next_switch;
  m_hover_tool_refresh = next_refresh;
  m_hover_tool_action_btn1 = next_btn1;
  m_hover_tool_action_btn2 = next_btn2;

  return changed;
}

SidebarPressResult ToolSidebar::handle_tool_plugin_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;
  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  const UI::Rect switch_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};

  if (switch_rect.contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true, .action = SidebarActionKind::SwitchTool};
  }
  if (refresh_rect.contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true, .action = SidebarActionKind::Refresh};
  }

  float cur_y = panel.y + header_height * scale + 12.0F * scale + 52.0F * scale + 14.0F * scale;
  const float content_left = panel.x + 12.0F * scale;
  const float content_w = std::max(panel.width - 24.0F * scale, 0.0F);

  // Section 1 header
  const UI::Rect sec1_header{content_left, cur_y, content_w, 24.0F * scale};
  if (sec1_header.contains(point_x, point_y)) {
    m_tool_section1_expanded = !m_tool_section1_expanded;
    return SidebarPressResult{.handled = true};
  }

  cur_y += 26.0F * scale;
  if (m_tool_section1_expanded) {
    cur_y += 34.0F * scale * 3;
  }
  cur_y += 8.0F * scale;

  // Section 2 header
  const UI::Rect sec2_header{content_left, cur_y, content_w, 24.0F * scale};
  if (sec2_header.contains(point_x, point_y)) {
    m_tool_section2_expanded = !m_tool_section2_expanded;
    return SidebarPressResult{.handled = true};
  }

  cur_y += 26.0F * scale;
  if (m_tool_section2_expanded) {
    cur_y += 24.0F * scale * 3;
  }
  cur_y += 16.0F * scale;

  const UI::Rect btn1_rect{content_left, cur_y, content_w, 26.0F * scale};
  if (btn1_rect.contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true, .action = SidebarActionKind::OpenTerminal};
  }

  const UI::Rect btn2_rect{content_left, cur_y + 32.0F * scale, content_w, 26.0F * scale};
  if (btn2_rect.contains(point_x, point_y)) {
    return SidebarPressResult{.handled = true};
  }

  return SidebarPressResult{.handled = true};
}


SidebarPressResult ToolSidebar::handle_pointer_press(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (is_resize_handle_point(layout, point_x, point_y)) {
    m_resizing = true;
    m_drag_start_x = point_x;
    m_drag_start_width = m_width;
    return SidebarPressResult{.handled = true};
  }
  if (!contains(layout, point_x, point_y)) {
    return SidebarPressResult{.handled = false};
  }

  m_is_focused = true;

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
    return handle_search_press(layout, point_x, point_y);
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    return handle_extensions_press(layout, point_x, point_y);
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::ToolPlugin) {
    return handle_tool_plugin_press(layout, point_x, point_y);
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      m_model.get_project_items().empty()) {
    const float scale = layout.dpi_scale;
    const UI::Rect panel = layout.tool_sidebar_bounds;
    const float msg_y = panel.y + (header_height + 22.0F) * scale;
    float btn_y = msg_y + 36.0F * scale;
    const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
    const float btn_h = 28.0F * scale;
    const float btn_x = panel.x + 14.0F * scale;

    m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
    btn_y += btn_h + 20.0F * scale + 14.0F * scale;
    m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});

    if (m_empty_state_open_btn.handle_pointer_press(point_x, point_y)) {
      return SidebarPressResult{.handled = true, .action = SidebarActionKind::OpenFile, .path = "::OPEN_FOLDER::"};
    }
    if (m_empty_state_clone_btn.handle_pointer_press(point_x, point_y)) {
      return SidebarPressResult{.handled = true};
    }
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const auto items = m_model.get_project_items();
    const std::size_t row_count = viewport_row_count(layout);
    m_project_scrollbar.set_track_bounds(scrollbar_bounds(layout));
    m_project_scrollbar.set_metrics(items.size(), row_count);
    static_cast<void>(m_project_scrollbar.scroll_to(m_model.get_scroll_offset()));
    if (m_project_scrollbar.handle_pointer_press(point_x, point_y)) {
      m_model.set_scroll_offset(m_project_scrollbar.get_scroll_offset());
      return SidebarPressResult{.handled = true};
    }
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const bool show_actions = !m_model.get_project_items().empty();
    HeaderAction header_act = HeaderAction::None;
    if (m_explorer_header.handle_pointer_press(layout, point_x, point_y, m_model, header_act, show_actions)) {
      if (header_act == HeaderAction::NewFile) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::NewFile, .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::NewFolder) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::NewFolder, .path = m_model.get_target_directory_for_creation()};
      }
      if (header_act == HeaderAction::Refresh) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::Refresh};
      }
      if (header_act == HeaderAction::CollapseAll) {
        return SidebarPressResult{.handled = true, .action = SidebarActionKind::CollapseAll};
      }
      return SidebarPressResult{.handled = true};
    }
  }
  const float scale = layout.dpi_scale;
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  const auto sticky = get_sticky_items();
  const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;
  
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project &&
      point_y >= tree_top && point_y < tree_top + sticky_height) {
      std::size_t sticky_index = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
      if (sticky_index < sticky.size()) {
          m_model.set_scroll_offset(sticky[sticky_index]);
          return SidebarPressResult{.handled = true};
      }
  }

  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row && m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    m_drag_source_row = *row;
    m_drag_press_x = point_x;
    m_drag_press_y = point_y;
    m_drag_current_x = point_x;
    m_drag_current_y = point_y;
    m_is_dragging_item = false;
    m_drag_target_row.reset();

    const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl_down = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const UI::Editor::ActivityPanelAction action =
        m_model.activate_project_row(*row, shift_down, ctrl_down);
    if (action.file_to_open) {
      return SidebarPressResult{.handled = true, .action = SidebarActionKind::OpenFile, .path = action.file_to_open};
    }
    return SidebarPressResult{.handled = action.handled};
  }
  return SidebarPressResult{.handled = true};
}

std::optional<std::filesystem::path> ToolSidebar::handle_right_click(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) {
  if (!contains(layout, point_x, point_y) || m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    return std::nullopt;
  }
  const std::optional<std::size_t> row = row_from_point(layout, point_y);
  if (row) {
    const std::size_t item_index = m_model.get_scroll_offset() + *row;
    const auto items = m_model.get_project_items();
    if (item_index < items.size()) {
      if (!m_model.is_selected(items[item_index].path)) {
        m_model.select_single(items[item_index].path);
      }
      return items[item_index].path;
    }
  }
  m_model.set_selected_path(m_model.get_workspace_root());
  return m_model.get_workspace_root();
}

bool ToolSidebar::handle_pointer_move(
    const UI::Editor::StudioEditorLayoutResult &layout, float point_x,
    float point_y) noexcept {
    std::optional<UI::Editor::SidebarIcon> next_icon;
    if (const std::optional<std::size_t> sidebar_index =
            UI::Editor::hit_test_studio_sidebar(layout, point_x, point_y)) {
        next_icon = UI::Editor::get_studio_sidebar_items()[*sidebar_index].icon;
    }

    const bool next_resize_hovered = is_resize_handle_point(layout, point_x, point_y);
    const bool resize_changed = (next_resize_hovered != m_resize_hovered);
    m_resize_hovered = next_resize_hovered;

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
      const bool search_changed = handle_search_move(layout, point_x, point_y);
      const bool icon_changed = next_icon != m_hovered_icon;
      m_hovered_icon = next_icon;
      return search_changed || icon_changed || resize_changed;
    }

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
      const bool ext_changed = handle_extensions_move(layout, point_x, point_y);
      const bool icon_changed = next_icon != m_hovered_icon;
      m_hovered_icon = next_icon;
      return ext_changed || icon_changed || resize_changed;
    }

    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::ToolPlugin) {
      const bool tool_changed = handle_tool_plugin_move(layout, point_x, point_y);
      const bool icon_changed = next_icon != m_hovered_icon;
      m_hovered_icon = next_icon;
      return tool_changed || icon_changed || resize_changed;
    }

    std::optional<std::size_t> next_sticky_hover;
    std::optional<std::size_t> next_row;
    bool next_scrollbar = false;
    
    if (contains(layout, point_x, point_y) &&
        m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
        
        const float scale = layout.dpi_scale;
        const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
        const auto sticky = get_sticky_items();
        const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;

        if (point_y >= tree_top && point_y < tree_top + sticky_height) {
            std::size_t sticky_index = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
            if (sticky_index < sticky.size()) {
                next_sticky_hover = sticky[sticky_index];
            }
        } else {
            next_row = row_from_point(layout, point_y);
        }
        
        next_scrollbar =
            scrollbar_bounds(layout).contains(point_x, point_y) &&
            m_model.get_project_items().size() > viewport_row_count(layout);
        if (next_scrollbar) {
            next_row.reset();
        }
    }
    
    bool header_changed = false;
    bool btn_changed = false;
    if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
      const bool show_actions = !m_model.get_project_items().empty();
      header_changed = m_explorer_header.handle_pointer_move(layout, point_x, point_y, show_actions);

      if (m_model.get_project_items().empty()) {
        const bool prev_open_h = m_empty_state_open_btn.get_state().hovered;
        const bool prev_clone_h = m_empty_state_clone_btn.get_state().hovered;
        static_cast<void>(m_empty_state_open_btn.handle_pointer_move(point_x, point_y));
        static_cast<void>(m_empty_state_clone_btn.handle_pointer_move(point_x, point_y));
        btn_changed = (prev_open_h != m_empty_state_open_btn.get_state().hovered) ||
                      (prev_clone_h != m_empty_state_clone_btn.get_state().hovered);
      }
    }

    const bool changed = (next_row != m_hovered_row) ||
                         (next_sticky_hover != m_hovered_sticky_index) ||
                         (next_icon != m_hovered_icon) ||
                         (next_scrollbar != m_hovered_scrollbar) ||
                         resize_changed ||
                         header_changed ||
                         btn_changed;
    m_hovered_row = next_row;
    m_hovered_sticky_index = next_sticky_hover;
    m_hovered_icon = next_icon;
    m_hovered_scrollbar = next_scrollbar;
    return changed;
}

bool ToolSidebar::handle_scroll(
    const UI::Editor::StudioEditorLayoutResult &layout,
    std::ptrdiff_t line_delta) noexcept {
  m_hovered_row.reset();
  m_hovered_sticky_index.reset();
  m_hovered_search_row.reset();
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
    return m_search_model.scroll(line_delta, search_viewport_row_count(layout));
  }
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    return handle_extensions_scroll(layout, line_delta);
  }
  return m_model.scroll(line_delta, viewport_row_count(layout));
}

bool ToolSidebar::is_visible() const noexcept { return m_model.is_visible(); }
bool ToolSidebar::is_active(UI::Editor::SidebarIcon icon) const noexcept {
  return m_model.is_active(icon);
}
bool ToolSidebar::is_hovered(UI::Editor::SidebarIcon icon) const noexcept {
  return m_hovered_icon && *m_hovered_icon == icon;
}

bool ToolSidebar::contains(const UI::Editor::StudioEditorLayoutResult &layout,
                           float point_x, float point_y) const noexcept {
  return is_visible() && layout.tool_sidebar_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_interactive_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept {
  if (!is_visible() || !layout.tool_sidebar_bounds.contains(point_x, point_y)) {
    return false;
  }
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    return true;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::ToolPlugin) {
    return true;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const bool show_actions = !m_model.get_project_items().empty();
    if (m_explorer_header.is_interactive_point(layout, point_x, point_y, show_actions)) {
      return true;
    }
    if (m_model.get_project_items().empty()) {
      const float msg_y = panel.y + (header_height + 22.0F) * scale;
      float btn_y = msg_y + 36.0F * scale;
      const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
      const float btn_h = 28.0F * scale;
      const float btn_x = panel.x + 14.0F * scale;
      const UI::Rect open_btn{btn_x, btn_y, btn_w, btn_h};
      btn_y += btn_h + 20.0F * scale + 14.0F * scale;
      const UI::Rect clone_btn{btn_x, btn_y, btn_w, btn_h};
      return open_btn.contains(point_x, point_y) ||
             clone_btn.contains(point_x, point_y) ||
             m_empty_state_open_btn.get_bounds().contains(point_x, point_y) ||
             m_empty_state_clone_btn.get_bounds().contains(point_x, point_y);
    }
    const float tree_top = panel.y + header_height * scale;
    const auto sticky = get_sticky_items();
    const float sticky_height = static_cast<float>(sticky.size()) * row_height * scale;
    if (point_y >= tree_top && point_y < tree_top + sticky_height) {
      const std::size_t sticky_index = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
      if (sticky_index < sticky.size()) {
        return true;
      }
    }
    if (scrollbar_bounds(layout).contains(point_x, point_y) &&
        m_model.get_project_items().size() > viewport_row_count(layout)) {
      return true;
    }
    if (point_y >= tree_top && row_from_point(layout, point_y).has_value()) {
      return true;
    }
    return false;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
    const float header_center_y = panel.y + header_height * 0.5F * scale;
    const float btn_size = 20.0F * scale;

    // Search Header buttons: Collapse All, Clear, Refresh
    if (UI::Rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y) ||
        UI::Rect{panel.right() - 22.0F * scale - btn_size, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y) ||
        UI::Rect{panel.right() - 20.0F * scale, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y)) {
      return true;
    }

    const float input_top = panel.y + header_height * scale + 8.0F * scale;
    // Chevron expand/collapse toggle
    if (UI::Rect{panel.x + 6.0F * scale, input_top + 3.0F * scale, 18.0F * scale, 20.0F * scale}.contains(point_x, point_y)) {
      return true;
    }

    // Search option toggle buttons
    const UI::Rect search_bounds{panel.x + 28.0F * scale, input_top, std::max(panel.width - 36.0F * scale, 0.0F), 26.0F * scale};
    const float opt_btn_w = 20.0F * scale;
    const float opt_btn_h = 20.0F * scale;
    const float opt_btn_y = input_top + 3.0F * scale;

    if (UI::Rect{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y) ||
        UI::Rect{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y) ||
        UI::Rect{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h}.contains(point_x, point_y)) {
      return true;
    }

    if (m_search_model.is_replace_expanded()) {
      const float replace_top = input_top + 30.0F * scale;
      const UI::Rect replace_bounds{panel.x + 28.0F * scale, replace_top, std::max(panel.width - 64.0F * scale, 0.0F), 26.0F * scale};
      if (UI::Rect{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h}.contains(point_x, point_y) ||
          UI::Rect{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale}.contains(point_x, point_y)) {
        return true;
      }
    }

    // Results Tree rows
    if (search_row_from_point(layout, point_y).has_value()) {
      return true;
    }
    return false;
  }

  // Other sidebars (Source Control, Run/Debug, Extensions, Settings)
  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 20.0F * scale;
  if (UI::Rect{panel.right() - 24.0F * scale, header_center_y - btn_size * 0.5F, btn_size, btn_size}.contains(point_x, point_y)) {
    return true;
  }

  return false;
}

bool ToolSidebar::is_text_input_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept {
  if (!is_visible()) {
    return false;
  }
  const float scale = layout.dpi_scale;
  const UI::Rect panel = layout.tool_sidebar_bounds;

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    const float search_top = panel.y + header_height * scale + 8.0F * scale;
    const UI::Rect search_bounds{panel.x + 12.0F * scale, search_top, std::max(panel.width - 24.0F * scale, 0.0F), 26.0F * scale};
    const UI::Rect search_text_area{search_bounds.x, search_bounds.y, std::max(search_bounds.width - 40.0F * scale, 0.0F), search_bounds.height};
    return search_text_area.contains(point_x, point_y);
  }

  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Search) {
    return false;
  }

  const float input_top = panel.y + header_height * scale + 8.0F * scale;

  const UI::Rect search_bounds{panel.x + 28.0F * scale, input_top, std::max(panel.width - 36.0F * scale, 0.0F), 26.0F * scale};
  const UI::Rect search_text_area{search_bounds.x, search_bounds.y, std::max(search_bounds.width - 70.0F * scale, 0.0F), search_bounds.height};
  if (search_text_area.contains(point_x, point_y)) {
    return true;
  }

  if (m_search_model.is_replace_expanded()) {
    const float replace_top = input_top + 30.0F * scale;
    const UI::Rect replace_bounds{panel.x + 28.0F * scale, replace_top, std::max(panel.width - 64.0F * scale, 0.0F), 26.0F * scale};
    const UI::Rect replace_text_area{replace_bounds.x, replace_bounds.y, std::max(replace_bounds.width - 26.0F * scale, 0.0F), replace_bounds.height};
    if (replace_text_area.contains(point_x, point_y)) {
      return true;
    }
  }

  return false;
}

bool ToolSidebar::is_resize_handle_point(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) const noexcept {
    const float scale = layout.dpi_scale;
    const UI::Rect handle_bounds{
        layout.tool_sidebar_bounds.right() - 4.0F * scale,
        layout.tool_sidebar_bounds.y,
        8.0F * scale,
        layout.tool_sidebar_bounds.height
    };
    return handle_bounds.contains(point_x, point_y);
}

bool ToolSidebar::is_resizing() const noexcept { return m_resizing; }

float ToolSidebar::get_width() const noexcept { return m_width; }

bool ToolSidebar::handle_pointer_drag(
    const UI::Editor::StudioEditorLayoutResult& layout,
    float point_x,
    float point_y) noexcept {
    if (m_resizing) {
        const float delta = point_x - m_drag_start_x;
        float new_width = m_drag_start_width + delta / layout.dpi_scale;
        
        if (!m_model.is_visible()) {
            if (new_width >= 100.0F) {
                m_model.set_visible(true);
                m_width = new_width;
            } else {
                m_width = 0.0F;
            }
        } else {
            if (new_width < 100.0F) {
                m_model.set_visible(false);
                m_width = 0.0F;
            } else {
                m_width = new_width;
            }
        }
        return true;
    }

    if (m_is_selecting_search_text && m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
        const float scale = layout.dpi_scale;
        const float char_w = 7.2F * scale;
        if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search) {
            const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
            const UI::Rect search_bounds{layout.tool_sidebar_bounds.x + 28.0F * scale, input_top, std::max(layout.tool_sidebar_bounds.width - 36.0F * scale, 0.0F), 26.0F * scale};
            const float text_x = search_bounds.x + 6.0F * scale;
            const std::string_view q = m_search_model.get_search_query();
            std::size_t idx = 0;
            if (point_x > text_x) {
                idx = static_cast<std::size_t>((point_x - text_x + char_w * 0.5F) / char_w);
                idx = std::min(idx, q.size());
            }
            m_search_model.update_drag_selection(idx);
            return true;
        } else if (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace) {
            const float input_top = layout.tool_sidebar_bounds.y + header_height * scale + 8.0F * scale;
            const float replace_top = input_top + 30.0F * scale;
            const UI::Rect replace_bounds{layout.tool_sidebar_bounds.x + 28.0F * scale, replace_top, std::max(layout.tool_sidebar_bounds.width - 64.0F * scale, 0.0F), 26.0F * scale};
            const float text_x = replace_bounds.x + 6.0F * scale;
            const std::string_view q = m_search_model.get_replace_query();
            std::size_t idx = 0;
            if (point_x > text_x) {
                idx = static_cast<std::size_t>((point_x - text_x + char_w * 0.5F) / char_w);
                idx = std::min(idx, q.size());
            }
            m_search_model.update_drag_selection(idx);
            return true;
        }
    }

    if (m_project_scrollbar.is_dragging()) {
        m_project_scrollbar.set_track_bounds(scrollbar_bounds(layout));
        m_project_scrollbar.set_metrics(m_model.get_project_items().size(), viewport_row_count(layout));
        if (m_project_scrollbar.handle_pointer_drag(point_x, point_y)) {
            m_model.set_scroll_offset(m_project_scrollbar.get_scroll_offset());
        }
        return true;
    }

    if (m_search_scrollbar.is_dragging()) {
        if (m_search_scrollbar.handle_pointer_drag(point_x, point_y)) {
            m_search_model.set_scroll_offset(m_search_scrollbar.get_scroll_offset());
        }
        return true;
    }

    if (m_extensions_scrollbar.is_dragging()) {
        if (m_extensions_scrollbar.handle_pointer_drag(point_x, point_y)) {
            m_ext_scroll_y = static_cast<float>(m_extensions_scrollbar.get_scroll_offset());
        }
        return true;
    }

    if (m_drag_source_row.has_value() && m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
        const float dist = std::hypot(point_x - m_drag_press_x, point_y - m_drag_press_y);
        if (dist > 5.0F) {
            m_is_dragging_item = true;
            m_drag_current_x = point_x;
            m_drag_current_y = point_y;
            m_drag_target_row = row_from_point(layout, point_y);
            return true;
        }
    }
    
    return false;
}

bool ToolSidebar::handle_pointer_release() noexcept {
    const bool was_resizing = m_resizing;
    const bool was_dragging = m_is_dragging_item;
    const bool scroll_project_released = m_project_scrollbar.handle_pointer_release();
    const bool scroll_search_released = m_search_scrollbar.handle_pointer_release();
    const bool scroll_ext_released = m_extensions_scrollbar.handle_pointer_release();
    m_resizing = false;
    m_is_selecting_search_text = false;

    if (m_is_dragging_item && m_drag_source_row.has_value()) {
        const auto items = m_model.get_project_items();
        const std::size_t source_idx = m_model.get_scroll_offset() + *m_drag_source_row;
        if (source_idx < items.size()) {
            const auto source_path = items[source_idx].path;
            std::filesystem::path target_dir;
            if (m_drag_target_row.has_value()) {
                const std::size_t target_idx = m_model.get_scroll_offset() + *m_drag_target_row;
                if (target_idx < items.size()) {
                    const auto& target_item = items[target_idx];
                    if (target_item.directory) {
                        target_dir = target_item.path;
                    } else {
                        target_dir = target_item.path.parent_path();
                    }
                }
            } else {
                target_dir = m_model.get_workspace_root();
            }

            if (!target_dir.empty()) {
                const auto selected = m_model.get_selected_paths();
                if (selected.size() > 1 && m_model.is_selected(source_path)) {
                    for (const auto& p : selected) {
                        if (p != target_dir && !target_dir.string().starts_with(p.string()) && p.parent_path() != target_dir) {
                            std::filesystem::path out_p;
                            static_cast<void>(m_model.move_item(p, target_dir, out_p));
                        }
                    }
                } else if (source_path != target_dir && source_path.parent_path() != target_dir) {
                    std::filesystem::path out_p;
                    static_cast<void>(m_model.move_item(source_path, target_dir, out_p));
                }
            }
        }
    }
    m_drag_source_row.reset();
    m_drag_target_row.reset();
    m_is_dragging_item = false;
    return was_resizing || was_dragging || scroll_project_released || scroll_search_released || scroll_ext_released;
}

bool ToolSidebar::tick_animations() noexcept {
    bool updated = false;
    if (m_search_model.tick()) {
        updated = true;
    }
    // Animate caret blinking and refresh immediately while search worker is running or user is focused on search
    if ((m_model.get_active_icon() == UI::Editor::SidebarIcon::Search &&
        (m_search_model.get_focused_input() != UI::Editor::SearchInputFocus::None || m_search_model.is_searching())) ||
        (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services && m_ext_search_focused)) {
        updated = true;
    }
    if (is_visible() && !m_model.get_workspace_root().empty()) {
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_refresh_time).count() >= 1000) {
            m_last_refresh_time = now;
            if (m_model.refresh()) {
                updated = true;
            }
        }
    }
    return updated;
}

void ToolSidebar::render_search_panel(
    const StudioWorkspaceRenderer &surface, HDC device_context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;

  // 1. Header: "Search" with Action Buttons (Collapse All, Clear, Refresh)
  surface.draw_text(device_context, *surface.m_ui_font, "Search",
                    panel.x + 14.0F * scale,
                    panel.y + header_height * 0.5F * scale,
                    surface.m_palette.text_primary);

  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  // Collapse All Icon
  const UI::Rect collapse_rect{panel.right() - 24.0F * scale - btn_size * 2.0F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_search_collapse_all) {
    surface.fill_rounded_rectangle(device_context, collapse_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "collapse-all.svg",
                        round_to_int(collapse_rect.x + btn_size * 0.5F),
                        round_to_int(collapse_rect.y + btn_size * 0.5F),
                        round_to_int(13.0F * scale),
                        m_hover_search_collapse_all ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Clear Search Icon
  const UI::Rect clear_rect{panel.right() - 22.0F * scale - btn_size, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_search_clear) {
    surface.fill_rounded_rectangle(device_context, clear_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "close-minimal.svg",
                        round_to_int(clear_rect.x + btn_size * 0.5F),
                        round_to_int(clear_rect.y + btn_size * 0.5F),
                        round_to_int(12.0F * scale),
                        m_hover_search_clear ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Refresh Icon
  const UI::Rect refresh_rect{panel.right() - 20.0F * scale, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_search_refresh) {
    surface.fill_rounded_rectangle(device_context, refresh_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "refresh.svg",
                        round_to_int(refresh_rect.x + btn_size * 0.5F),
                        round_to_int(refresh_rect.y + btn_size * 0.5F),
                        round_to_int(12.0F * scale),
                        m_hover_search_refresh ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  surface.draw_line(device_context, round_to_int(panel.x),
                    round_to_int(panel.y + header_height * scale),
                    round_to_int(panel.right()),
                    round_to_int(panel.y + header_height * scale),
                    surface.m_palette.border);

  // 2. Search Box & Replace Row
  const float input_top = panel.y + header_height * scale + 8.0F * scale;

  // Chevron Expand/Collapse Toggle on left
  const UI::Rect chevron_bounds{panel.x + 6.0F * scale, input_top + 3.0F * scale, 18.0F * scale, 20.0F * scale};
  if (m_hover_search_chevron) {
    surface.fill_rounded_rectangle(device_context, chevron_bounds, surface.m_palette.hover_background, 3.0F * scale);
  }
  const std::string search_chevron = m_search_model.is_replace_expanded() ? "chevron-down.svg" : "chevron-right.svg";
  surface.draw_svg_icon(
      device_context, search_chevron,
      round_to_int(chevron_bounds.x + chevron_bounds.width * 0.5F),
      round_to_int(chevron_bounds.y + chevron_bounds.height * 0.5F),
      std::max(round_to_int(9.0F * scale), 8),
      m_hover_search_chevron ? surface.m_palette.text_primary : surface.m_palette.text_muted,
      surface.m_palette.sidebar_background);

  // Search Input Box
  const UI::Rect search_bounds{
      panel.x + 28.0F * scale,
      input_top,
      std::max(panel.width - 36.0F * scale, 0.0F),
      26.0F * scale
  };
  const bool search_focused = (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Search);
  const bool caret_visible = ((GetTickCount64() / 500) % 2) == 0;

  surface.fill_rounded_rectangle(device_context, search_bounds, surface.m_palette.editor_background, 3.0F * scale);
  const UI::Theme::Color search_border_col = search_focused
      ? UI::Theme::Color{59, 130, 246, 230}
      : surface.m_palette.border;
  surface.draw_rectangle(device_context, search_bounds, search_border_col);

  // Search text or placeholder
  const std::string_view query = m_search_model.get_search_query();
  if (query.empty()) {
    surface.draw_text(device_context, *surface.m_small_font, "Search (e.g. settings)",
                      search_bounds.x + 6.0F * scale,
                      search_bounds.y + search_bounds.height * 0.5F,
                      surface.m_palette.text_muted);
    if (search_focused && caret_visible) {
      surface.draw_line(device_context, round_to_int(search_bounds.x + 6.0F * scale),
                        round_to_int(search_bounds.y + 4.0F * scale),
                        round_to_int(search_bounds.x + 6.0F * scale),
                        round_to_int(search_bounds.bottom() - 4.0F * scale),
                        UI::Theme::Color{255, 255, 255, 255});
    }
  } else {
    // Selection highlight
    if (search_focused && m_search_model.has_selection()) {
      const auto [s_min, s_max] = m_search_model.get_selection_range();
      const int w_before = surface.m_small_font->getTextWidth(device_context, std::string{query.substr(0, s_min)});
      const int w_sel = surface.m_small_font->getTextWidth(device_context, std::string{query.substr(s_min, s_max - s_min)});
      const UI::Rect sel_rect{
          search_bounds.x + 6.0F * scale + static_cast<float>(w_before),
          search_bounds.y + 3.0F * scale,
          static_cast<float>(w_sel),
          search_bounds.height - 6.0F * scale
      };
      surface.fill_rounded_rectangle(device_context, sel_rect, UI::Theme::Color{59, 130, 246, 110}, 2.0F * scale);
    }

    surface.draw_text(device_context, *surface.m_small_font, query,
                      search_bounds.x + 6.0F * scale,
                      search_bounds.y + search_bounds.height * 0.5F,
                      surface.m_palette.text_primary);
    if (search_focused && caret_visible && !m_search_model.has_selection()) {
      const int text_w = surface.m_small_font->getTextWidth(device_context, std::string{query.substr(0, m_search_model.get_search_caret())});
      const float caret_x = search_bounds.x + 6.0F * scale + static_cast<float>(text_w);
      surface.draw_line(device_context, round_to_int(caret_x),
                        round_to_int(search_bounds.y + 4.0F * scale),
                        round_to_int(caret_x),
                        round_to_int(search_bounds.bottom() - 4.0F * scale),
                        UI::Theme::Color{255, 255, 255, 255});
    }
  }

  // Toggles inside Search Box: [Aa] [ab|] [.*]
  const float opt_btn_w = 20.0F * scale;
  const float opt_btn_h = 20.0F * scale;
  const float opt_btn_y = input_top + 3.0F * scale;

  // [.*] (Use Regular Expression)
  const UI::Rect regex_bounds{search_bounds.right() - 22.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (m_search_model.is_use_regex()) {
    surface.fill_rounded_rectangle(device_context, regex_bounds, UI::Theme::Color{59, 130, 246, 75}, 2.5F * scale);
    surface.draw_rectangle(device_context, regex_bounds, UI::Theme::Color{59, 130, 246, 220});
  } else if (m_hover_search_use_regex) {
    surface.fill_rounded_rectangle(device_context, regex_bounds, surface.m_palette.hover_background, 2.5F * scale);
  }
  surface.draw_text(device_context, *surface.m_small_font, ".*",
                    regex_bounds.x + 4.0F * scale, regex_bounds.y + regex_bounds.height * 0.5F,
                    m_search_model.is_use_regex() ? UI::Theme::Color{255, 255, 255, 255} : (m_hover_search_use_regex ? surface.m_palette.text_primary : surface.m_palette.text_muted));

  // [ab|] (Match Whole Word)
  const UI::Rect word_bounds{search_bounds.right() - 44.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (m_search_model.is_match_word()) {
    surface.fill_rounded_rectangle(device_context, word_bounds, UI::Theme::Color{59, 130, 246, 75}, 2.5F * scale);
    surface.draw_rectangle(device_context, word_bounds, UI::Theme::Color{59, 130, 246, 220});
  } else if (m_hover_search_match_word) {
    surface.fill_rounded_rectangle(device_context, word_bounds, surface.m_palette.hover_background, 2.5F * scale);
  }
  surface.draw_text(device_context, *surface.m_small_font, "ab",
                    word_bounds.x + 3.0F * scale, word_bounds.y + word_bounds.height * 0.5F,
                    m_search_model.is_match_word() ? UI::Theme::Color{255, 255, 255, 255} : (m_hover_search_match_word ? surface.m_palette.text_primary : surface.m_palette.text_muted));

  // [Aa] (Match Case)
  const UI::Rect case_bounds{search_bounds.right() - 66.0F * scale, opt_btn_y, opt_btn_w, opt_btn_h};
  if (m_search_model.is_match_case()) {
    surface.fill_rounded_rectangle(device_context, case_bounds, UI::Theme::Color{59, 130, 246, 75}, 2.5F * scale);
    surface.draw_rectangle(device_context, case_bounds, UI::Theme::Color{59, 130, 246, 220});
  } else if (m_hover_search_match_case) {
    surface.fill_rounded_rectangle(device_context, case_bounds, surface.m_palette.hover_background, 2.5F * scale);
  }
  surface.draw_text(device_context, *surface.m_small_font, "Aa",
                    case_bounds.x + 3.0F * scale, case_bounds.y + case_bounds.height * 0.5F,
                    m_search_model.is_match_case() ? UI::Theme::Color{255, 255, 255, 255} : (m_hover_search_match_case ? surface.m_palette.text_primary : surface.m_palette.text_muted));

  // Replace Row (when expanded)
  if (m_search_model.is_replace_expanded()) {
    const float replace_top = input_top + 30.0F * scale;
    const UI::Rect replace_bounds{
        panel.x + 28.0F * scale,
        replace_top,
        std::max(panel.width - 64.0F * scale, 0.0F),
        26.0F * scale
    };
    const bool replace_focused = (m_search_model.get_focused_input() == UI::Editor::SearchInputFocus::Replace);
    surface.fill_rounded_rectangle(device_context, replace_bounds, surface.m_palette.editor_background, 3.0F * scale);
    const UI::Theme::Color replace_border_col = replace_focused
        ? UI::Theme::Color{59, 130, 246, 230}
        : surface.m_palette.border;
    surface.draw_rectangle(device_context, replace_bounds, replace_border_col);

    const std::string_view replace_query = m_search_model.get_replace_query();
    if (replace_query.empty()) {
      surface.draw_text(device_context, *surface.m_small_font, "Replace",
                        replace_bounds.x + 6.0F * scale,
                        replace_bounds.y + replace_bounds.height * 0.5F,
                        surface.m_palette.text_muted);
      if (replace_focused && caret_visible) {
        surface.draw_line(device_context, round_to_int(replace_bounds.x + 6.0F * scale),
                          round_to_int(replace_bounds.y + 4.0F * scale),
                          round_to_int(replace_bounds.x + 6.0F * scale),
                          round_to_int(replace_bounds.bottom() - 4.0F * scale),
                          UI::Theme::Color{255, 255, 255, 255});
      }
    } else {
      // Selection highlight
      if (replace_focused && m_search_model.has_selection()) {
        const auto [s_min, s_max] = m_search_model.get_selection_range();
        const int w_before = surface.m_small_font->getTextWidth(device_context, std::string{replace_query.substr(0, s_min)});
        const int w_sel = surface.m_small_font->getTextWidth(device_context, std::string{replace_query.substr(s_min, s_max - s_min)});
        const UI::Rect sel_rect{
            replace_bounds.x + 6.0F * scale + static_cast<float>(w_before),
            replace_top + 3.0F * scale,
            static_cast<float>(w_sel),
            replace_bounds.height - 6.0F * scale
        };
        surface.fill_rounded_rectangle(device_context, sel_rect, UI::Theme::Color{59, 130, 246, 110}, 2.0F * scale);
      }

      surface.draw_text(device_context, *surface.m_small_font, replace_query,
                        replace_bounds.x + 6.0F * scale,
                        replace_bounds.y + replace_bounds.height * 0.5F,
                        surface.m_palette.text_primary);
      if (replace_focused && caret_visible && !m_search_model.has_selection()) {
        const int text_w = surface.m_small_font->getTextWidth(device_context, std::string{replace_query.substr(0, m_search_model.get_replace_caret())});
        const float caret_x = replace_bounds.x + 6.0F * scale + static_cast<float>(text_w);
        surface.draw_line(device_context, round_to_int(caret_x),
                          round_to_int(replace_bounds.y + 4.0F * scale),
                          round_to_int(caret_x),
                          round_to_int(replace_bounds.bottom() - 4.0F * scale),
                          UI::Theme::Color{255, 255, 255, 255});
      }
    }

    // [AB] (Preserve Case)
    const UI::Rect preserve_bounds{replace_bounds.right() - 22.0F * scale, replace_top + 3.0F * scale, opt_btn_w, opt_btn_h};
    if (m_search_model.is_preserve_case()) {
      surface.fill_rounded_rectangle(device_context, preserve_bounds, UI::Theme::Color{59, 130, 246, 75}, 2.5F * scale);
      surface.draw_rectangle(device_context, preserve_bounds, UI::Theme::Color{59, 130, 246, 220});
    } else if (m_hover_search_preserve_case) {
      surface.fill_rounded_rectangle(device_context, preserve_bounds, surface.m_palette.hover_background, 2.5F * scale);
    }
    surface.draw_text(device_context, *surface.m_small_font, "AB",
                      preserve_bounds.x + 3.0F * scale, preserve_bounds.y + preserve_bounds.height * 0.5F,
                      m_search_model.is_preserve_case() ? UI::Theme::Color{255, 255, 255, 255} : (m_hover_search_preserve_case ? surface.m_palette.text_primary : surface.m_palette.text_muted));

    // Replace All Action Button
    const UI::Rect replace_all_bounds{panel.right() - 32.0F * scale, replace_top + 2.0F * scale, 22.0F * scale, 22.0F * scale};
    if (m_hover_search_replace_all) {
      surface.fill_rounded_rectangle(device_context, replace_all_bounds, surface.m_palette.hover_background, 3.0F * scale);
    }
    surface.draw_svg_icon(device_context, "refresh.svg",
                          round_to_int(replace_all_bounds.x + 11.0F * scale),
                          round_to_int(replace_all_bounds.y + 11.0F * scale),
                          round_to_int(13.0F * scale),
                          m_hover_search_replace_all ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.sidebar_background);
  }

  // 3. Results Summary Line
  const float summary_top = (m_search_model.is_replace_expanded() ? (input_top + 30.0F * scale + 30.0F * scale) : (input_top + 30.0F * scale)) + 4.0F * scale;

  if (!m_search_model.get_search_error().empty()) {
    surface.draw_text(device_context, *surface.m_small_font,
                      "Regex Error: " + m_search_model.get_search_error(),
                      panel.x + 14.0F * scale, summary_top + 10.0F * scale,
                      UI::Theme::Color{248, 113, 113, 255});
  } else if (!m_search_model.get_search_query().empty()) {
    std::string summary_text = std::to_string(m_search_model.get_total_match_count()) +
                               " results in " +
                               std::to_string(m_search_model.get_total_file_count()) + " files";
    surface.draw_text(device_context, *surface.m_small_font, summary_text,
                      panel.x + 14.0F * scale, summary_top + 10.0F * scale,
                      surface.m_palette.text_primary);
  }

  // 4. Tree Results View
  const float tree_top = search_tree_top_y(layout);
  const auto visible_rows = m_search_model.get_visible_rows();
  const auto& results = m_search_model.get_results();

  if (visible_rows.empty()) {
    if (m_search_model.get_search_query().empty()) {
      surface.draw_text(device_context, *surface.m_ui_font, "Search across workspace",
                        panel.x + 14.0F * scale, tree_top + 20.0F * scale,
                        surface.m_palette.text_primary);
      surface.draw_text(device_context, *surface.m_small_font, "Search results will appear in this panel.",
                        panel.x + 14.0F * scale, tree_top + 42.0F * scale,
                        surface.m_palette.text_muted);
    } else if (!m_search_model.is_searching()) {
      surface.draw_text(device_context, *surface.m_small_font, "No results found.",
                        panel.x + 14.0F * scale, tree_top + 14.0F * scale,
                        surface.m_palette.text_muted);
    }
    return;
  }

  const std::size_t first = m_search_model.get_scroll_offset();
  const std::size_t row_count = search_viewport_row_count(layout);
  const std::size_t end = std::min(visible_rows.size(), first + row_count);

  // Clip tree view strictly to sidebar bounds
  const int clip_saved = SaveDC(device_context);
  IntersectClipRect(device_context,
                    round_to_int(panel.x),
                    round_to_int(tree_top),
                    round_to_int(panel.right()),
                    round_to_int(panel.bottom()));

  for (std::size_t item_index = first; item_index < end; ++item_index) {
    const std::size_t visible_row = item_index - first;
    const auto& v_row = visible_rows[item_index];
    const float row_y = tree_top + static_cast<float>(visible_row) * row_height * scale;
    const UI::Rect row_bounds{panel.x, row_y, panel.width, row_height * scale};
    const bool is_hovered = (m_hovered_search_row && *m_hovered_search_row == item_index);

    if (is_hovered) {
      surface.fill_rounded_rectangle(device_context,
          UI::Rect{panel.x + 4.0F * scale, row_y + 1.0F * scale, panel.width - 8.0F * scale, row_height * scale - 2.0F * scale},
          UI::Theme::Color{255, 255, 255, 14}, 3.0F * scale);
    }

    if (v_row.kind == UI::Editor::SearchRowKind::FileHeader) {
      const auto& file = results[v_row.file_index];

      // Expand/Collapse Chevron SVG
      const std::string file_chevron = file.expanded ? "chevron-down.svg" : "chevron-right.svg";
      surface.draw_svg_icon(device_context, file_chevron,
                            round_to_int(panel.x + 12.0F * scale),
                            round_to_int(row_y + row_height * 0.5F * scale),
                            std::max(round_to_int(9.0F * scale), 8),
                            surface.m_palette.text_muted,
                            surface.m_palette.sidebar_background);

      // File Icon
      const std::string icon_asset = UI::Editor::file_icon_asset_for_path(file.file_path);
      surface.draw_svg_icon(device_context, icon_asset,
                            round_to_int(panel.x + 24.0F * scale),
                            round_to_int(row_y + row_height * 0.5F * scale),
                            round_to_int(14.0F * scale),
                            surface.m_palette.text_primary,
                            surface.m_palette.sidebar_background,
                            true);

      // File Name
      surface.draw_text(device_context, *surface.m_small_font, file.file_name,
                        panel.x + 36.0F * scale, row_y + row_height * 0.5F * scale,
                        surface.m_palette.text_primary);

      const int fname_w = surface.m_small_font->getTextWidth(device_context, file.file_name);

      // Directory Path in muted text
      if (!file.relative_dir.empty()) {
        const float dir_x = panel.x + 42.0F * scale + static_cast<float>(fname_w);
        const int avail_dir_w = round_to_int(panel.right() - dir_x - 38.0F * scale);
        if (avail_dir_w > 20) {
          const std::string dir_text = ellipsize(device_context, *surface.m_small_font, file.relative_dir, avail_dir_w);
          surface.draw_text(device_context, *surface.m_small_font, dir_text,
                            dir_x, row_y + row_height * 0.5F * scale,
                            surface.m_palette.text_muted);
        }
      }

      // Match Count Badge Pill on Right
      const std::string count_str = std::to_string(file.matches.size());
      const int count_w = surface.m_small_font->getTextWidth(device_context, count_str);
      const float pill_w = std::max(static_cast<float>(count_w) + 8.0F * scale, 18.0F * scale);
      const UI::Rect pill_bounds{panel.right() - pill_w - 8.0F * scale, row_y + 2.0F * scale, pill_w, 18.0F * scale};
      surface.fill_rounded_rectangle(device_context, pill_bounds, UI::Theme::Color{255, 255, 255, 22}, 9.0F * scale);
      surface.draw_text(device_context, *surface.m_small_font, count_str,
                        pill_bounds.x + (pill_bounds.width - static_cast<float>(count_w)) * 0.5F,
                        row_y + row_height * 0.5F * scale,
                        surface.m_palette.text_primary);
    } else {
      // Match Line Row
      const auto& file = results[v_row.file_index];
      if (v_row.match_index < file.matches.size()) {
        const auto& match = file.matches[v_row.match_index];

        // Line number (e.g. "45:")
        const std::string lnum_str = std::to_string(match.line_number) + ":";
        surface.draw_text(device_context, *surface.m_small_font, lnum_str,
                          panel.x + 24.0F * scale, row_y + row_height * 0.5F * scale,
                          surface.m_palette.text_muted);

        const int lnum_w = surface.m_small_font->getTextWidth(device_context, lnum_str);
        float text_cursor_x = panel.x + 28.0F * scale + static_cast<float>(lnum_w);
        const float max_right = panel.right() - 14.0F * scale;

        const std::string& line_text = match.line_content;
        std::size_t curr_pos = 0;

        const auto spans_to_draw = match.spans.empty()
            ? std::vector<UI::Editor::SearchHighlightSpan>{UI::Editor::SearchHighlightSpan{match.match_preview_start, match.match_preview_length}}
            : match.spans;

        for (const auto& span : spans_to_draw) {
          if (text_cursor_x >= max_right) break;

          // 1. Text before this match span
          if (span.start > curr_pos && curr_pos < line_text.size()) {
            const std::string before = line_text.substr(curr_pos, span.start - curr_pos);
            const int avail_w = round_to_int(max_right - text_cursor_x);
            if (avail_w <= 0) break;
            const int before_w = surface.m_small_font->getTextWidth(device_context, before);
            if (before_w <= avail_w) {
              surface.draw_text(device_context, *surface.m_small_font, before,
                                text_cursor_x, row_y + row_height * 0.5F * scale,
                                surface.m_palette.text_primary);
              text_cursor_x += static_cast<float>(before_w);
            } else {
              const std::string truncated = ellipsize(device_context, *surface.m_small_font, before, avail_w);
              surface.draw_text(device_context, *surface.m_small_font, truncated,
                                text_cursor_x, row_y + row_height * 0.5F * scale,
                                surface.m_palette.text_primary);
              text_cursor_x = max_right;
              break;
            }
          }

          if (text_cursor_x >= max_right) break;

          // 2. Highlighting Matched Span (soft elegant gold highlight)
          if (span.start < line_text.size()) {
            std::string match_word = line_text.substr(span.start, span.length);
            int word_w = surface.m_small_font->getTextWidth(device_context, match_word);
            const int avail_w = round_to_int(max_right - text_cursor_x);
            if (avail_w <= 0) break;

            if (word_w > avail_w) {
              match_word = ellipsize(device_context, *surface.m_small_font, match_word, avail_w);
              word_w = surface.m_small_font->getTextWidth(device_context, match_word);
            }

            const UI::Rect highlight_pill{
                text_cursor_x - 1.0F * scale,
                row_y + 3.0F * scale,
                static_cast<float>(word_w) + 2.0F * scale,
                row_height * scale - 6.0F * scale
            };
            // Soft amber / gold highlight
            surface.fill_rounded_rectangle(device_context, highlight_pill, UI::Theme::Color{234, 179, 8, 45}, 2.0F * scale);
            surface.draw_rectangle(device_context, highlight_pill, UI::Theme::Color{234, 179, 8, 120});
            surface.draw_text(device_context, *surface.m_small_font, match_word,
                              text_cursor_x, row_y + row_height * 0.5F * scale,
                              UI::Theme::Color{252, 211, 77, 255});
            text_cursor_x += static_cast<float>(word_w);
          }

          curr_pos = span.start + span.length;
        }

        // 3. Trailing text after all spans
        if (curr_pos < line_text.size() && text_cursor_x < max_right) {
          const std::string trailing = line_text.substr(curr_pos);
          const int avail_w = round_to_int(max_right - text_cursor_x);
          if (avail_w > 10) {
            const std::string truncated = ellipsize(device_context, *surface.m_small_font, trailing, avail_w);
            surface.draw_text(device_context, *surface.m_small_font, truncated,
                              text_cursor_x, row_y + row_height * 0.5F * scale,
                              surface.m_palette.text_primary);
          }
        }
      }
    }
  }

  RestoreDC(device_context, clip_saved);

  // Vertical Scrollbar for Search Results
  m_search_scrollbar.set_track_bounds(search_scrollbar_bounds(layout));
  m_search_scrollbar.set_metrics(visible_rows.size(), row_count);
  m_search_scrollbar.scroll_to(first);
  if (m_search_scrollbar.is_needed()) {
    const UI::Rect thumb_bounds = m_search_scrollbar.get_thumb_bounds();
    const UI::Theme::Color thumb_color = m_search_scrollbar.is_dragging()
        ? UI::Theme::Color{255, 255, 255, 115}
        : (m_hovered_search_scrollbar ? UI::Theme::Color{255, 255, 255, 75} : UI::Theme::Color{255, 255, 255, 40});
    surface.fill_rectangle(device_context, thumb_bounds, thumb_color);
  }

  // Draw Right Border separating sidebar from editor with blue accent highlight when hovered or resizing
  const bool show_accent = m_resize_hovered || m_resizing;
  const UI::Theme::Color splitter_color = show_accent
      ? surface.m_palette.accent
      : surface.m_palette.border;

  const float splitter_x = panel.right() - scale;
  surface.draw_line(device_context,
                    round_to_int(splitter_x),
                    round_to_int(panel.y),
                    round_to_int(splitter_x),
                    round_to_int(panel.bottom()),
                    splitter_color);

  if (show_accent) {
    surface.fill_rectangle(
        device_context,
        UI::Rect{
            splitter_x - 1.5F * scale,
            panel.y,
            std::max(3.5F * scale, 3.0F),
            panel.height},
        surface.m_palette.accent);
  }
}

void ToolSidebar::render_extensions_panel(
    const StudioWorkspaceRenderer &surface, HDC device_context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;

  // 1. Header: "Plugins" with Action Buttons (Refresh, More/Ellipsis)
  surface.draw_text(device_context, *surface.m_ui_font, "Plugins",
                    panel.x + 14.0F * scale,
                    panel.y + header_height * 0.5F * scale,
                    surface.m_palette.text_primary);

  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  // Refresh Icon
  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_ext_refresh) {
    surface.fill_rounded_rectangle(device_context, refresh_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "refresh.svg",
                        round_to_int(refresh_rect.x + btn_size * 0.5F),
                        round_to_int(refresh_rect.y + btn_size * 0.5F),
                        round_to_int(12.0F * scale),
                        m_hover_ext_refresh ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Ellipsis / More Icon
  const UI::Rect ellipsis_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_ext_more) {
    surface.fill_rounded_rectangle(device_context, ellipsis_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "ellipsis.svg",
                        round_to_int(ellipsis_rect.x + btn_size * 0.5F),
                        round_to_int(ellipsis_rect.y + btn_size * 0.5F),
                        round_to_int(14.0F * scale),
                        m_hover_ext_more ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Bottom border line for header
  surface.draw_line(device_context, round_to_int(panel.x),
                    round_to_int(panel.y + header_height * scale),
                    round_to_int(panel.right()),
                    round_to_int(panel.y + header_height * scale),
                    surface.m_palette.border);

  // 2. Search Box
  const float search_top = panel.y + header_height * scale + 8.0F * scale;
  const float search_height = 26.0F * scale;
  const UI::Rect search_bounds{
      panel.x + 12.0F * scale,
      search_top,
      std::max(panel.width - 24.0F * scale, 0.0F),
      search_height
  };

  surface.fill_rounded_rectangle(device_context, search_bounds, surface.m_palette.editor_background, 3.0F * scale);
  const UI::Theme::Color search_border_col = m_ext_search_focused
      ? UI::Theme::Color{59, 130, 246, 230}
      : surface.m_palette.border;
  surface.draw_rectangle(device_context, search_bounds, search_border_col);

  // Text inside search or placeholder
  const bool caret_visible = ((GetTickCount64() / 500) % 2) == 0;
  const float text_x = search_bounds.x + 8.0F * scale;
  const float text_y = search_bounds.y + search_bounds.height * 0.5F;

  if (m_ext_search_query.empty()) {
    const int avail_placeholder_w = round_to_int(search_bounds.width - 40.0F * scale);
    const std::string placeholder = ellipsize(device_context, *surface.m_small_font, "Search Plugins in Marketplace...", avail_placeholder_w);
    surface.draw_text(device_context, *surface.m_small_font, placeholder,
                      text_x, text_y, surface.m_palette.text_muted);
    if (m_ext_search_focused && caret_visible) {
      surface.draw_line(device_context, round_to_int(text_x),
                        round_to_int(search_bounds.y + 4.0F * scale),
                        round_to_int(text_x),
                        round_to_int(search_bounds.bottom() - 4.0F * scale),
                        UI::Theme::Color{255, 255, 255, 255});
    }
  } else {
    surface.draw_text(device_context, *surface.m_small_font, m_ext_search_query,
                      text_x, text_y, surface.m_palette.text_primary);
    if (m_ext_search_focused && caret_visible) {
      const int text_w = surface.m_small_font->getTextWidth(device_context, m_ext_search_query);
      const float caret_x = text_x + static_cast<float>(text_w);
      surface.draw_line(device_context, round_to_int(caret_x),
                        round_to_int(search_bounds.y + 4.0F * scale),
                        round_to_int(caret_x),
                        round_to_int(search_bounds.bottom() - 4.0F * scale),
                        UI::Theme::Color{255, 255, 255, 255});
    }

    // Clear icon (x)
    const UI::Rect clear_rect{search_bounds.right() - 36.0F * scale, search_top + 3.0F * scale, 16.0F * scale, 20.0F * scale};
    surface.draw_svg_icon(device_context, "close-minimal.svg",
                          round_to_int(clear_rect.x + clear_rect.width * 0.5F),
                          round_to_int(clear_rect.y + clear_rect.height * 0.5F),
                          round_to_int(10.0F * scale),
                          m_hover_ext_clear ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                          surface.m_palette.editor_background);
  }

  // Filter Icon inside Search Box
  const UI::Rect filter_rect{search_bounds.right() - 18.0F * scale, search_top + 3.0F * scale, 16.0F * scale, 20.0F * scale};
  surface.draw_svg_icon(device_context, "vscode-codicons/icons/filter.svg",
                        round_to_int(filter_rect.x + filter_rect.width * 0.5F),
                        round_to_int(filter_rect.y + filter_rect.height * 0.5F),
                        round_to_int(12.0F * scale),
                        m_hover_ext_filter ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.editor_background);

  // 3. Content Area
  const float content_top = search_top + search_height + 8.0F * scale;

  auto& pm = Zenvra::Plugins::PluginManager::instance();
  // Filter matching installed
  std::vector<std::shared_ptr<Zenvra::Plugins::Plugin>> matching_installed;
  for (const auto& p : pm.get_all_plugins()) {
    if (!p) continue;
    std::error_code ec;
    if (!std::filesystem::exists(p->get_install_path(), ec)) continue;
    if (!std::filesystem::exists(p->get_install_path() / "plugin.json", ec) &&
        !std::filesystem::exists(p->get_install_path() / "package.json", ec)) continue;

    if (m_ext_search_query.empty()) {
      matching_installed.push_back(p);
    } else {
      const std::string q = to_lower(m_ext_search_query);
      if (to_lower(p->get_name()).find(q) != std::string::npos ||
          to_lower(p->get_id()).find(q) != std::string::npos ||
          to_lower(p->get_description()).find(q) != std::string::npos ||
          to_lower(p->get_manifest().get_publisher()).find(q) != std::string::npos ||
          to_lower(p->get_manifest().get_category()).find(q) != std::string::npos) {
        matching_installed.push_back(p);
      }
    }
  }

  // Filter matching recommended
  std::vector<Zenvra::Plugins::Marketplace::MarketplacePluginEntry> matching_recommended;
  for (const auto& e : pm.get_marketplace().get_all_entries()) {
    if (pm.get_registry().has_plugin(e.id)) {
      auto p = pm.get_registry().get_plugin(e.id);
      std::error_code ec;
      if (p && std::filesystem::exists(p->get_install_path(), ec)) {
        continue;
      }
    }
    if (m_ext_search_query.empty()) {
      matching_recommended.push_back(e);
    } else {
      const std::string q = to_lower(m_ext_search_query);
      if (to_lower(e.name).find(q) != std::string::npos ||
          to_lower(e.id).find(q) != std::string::npos ||
          to_lower(e.description).find(q) != std::string::npos ||
          to_lower(e.publisher).find(q) != std::string::npos ||
          to_lower(e.category).find(q) != std::string::npos) {
        matching_recommended.push_back(e);
      }
    }
  }

  const int clip_saved = SaveDC(device_context);
  IntersectClipRect(device_context,
                    round_to_int(panel.x),
                    round_to_int(content_top),
                    round_to_int(panel.right()),
                    round_to_int(panel.bottom()));

  float cur_y = content_top - m_ext_scroll_y;
  const float header_h = 24.0F * scale;

  // --- SECTION 1: INSTALLED ---
  if (!matching_installed.empty() || m_ext_search_query.empty()) {
    const UI::Rect inst_hdr_rect{panel.x, cur_y, panel.width, header_h};
    if (m_hover_ext_installed_header) {
      surface.fill_rectangle(device_context, inst_hdr_rect, UI::Theme::Color{255, 255, 255, 8});
    }

    // Chevron
    const std::string chevron_icon = m_ext_installed_expanded ? "chevron-down.svg" : "chevron-right.svg";
    surface.draw_svg_icon(device_context, chevron_icon,
                          round_to_int(panel.x + 14.0F * scale),
                          round_to_int(cur_y + header_h * 0.5F),
                          std::max(round_to_int(9.0F * scale), 8),
                          surface.m_palette.text_primary,
                          surface.m_palette.sidebar_background);

    // Header Title
    surface.draw_text(device_context, *surface.m_ui_font, "Installed",
                      panel.x + 26.0F * scale, cur_y + header_h * 0.5F,
                      surface.m_palette.text_primary);

    // Count Badge Pill
    const std::string count_str = std::to_string(matching_installed.size());
    const int count_w = surface.m_small_font->getTextWidth(device_context, count_str);
    const float pill_w = std::max(static_cast<float>(count_w) + 10.0F * scale, 18.0F * scale);
    const UI::Rect pill_bounds{panel.right() - pill_w - 14.0F * scale, cur_y + 3.0F * scale, pill_w, 18.0F * scale};
    surface.fill_rounded_rectangle(device_context, pill_bounds, UI::Theme::Color{255, 255, 255, 25}, 9.0F * scale);
    surface.draw_text(device_context, *surface.m_small_font, count_str,
                      pill_bounds.x + (pill_bounds.width - static_cast<float>(count_w)) * 0.5F,
                      cur_y + header_h * 0.5F,
                      surface.m_palette.text_primary);

    cur_y += header_h;

    // Items
    if (m_ext_installed_expanded) {
      if (matching_installed.empty()) {
        surface.draw_text(device_context, *surface.m_small_font, "No installed plugins yet.",
                          panel.x + 26.0F * scale, cur_y + 14.0F * scale,
                          surface.m_palette.text_muted);
        cur_y += 28.0F * scale;
      } else {
        for (std::size_t i = 0; i < matching_installed.size(); ++i) {
          const auto& p = matching_installed[i];
          const float item_h = 62.0F * scale;

          if (cur_y + item_h >= content_top && cur_y <= panel.bottom()) {
            // Hover background
            if (m_hovered_ext_installed_idx && *m_hovered_ext_installed_idx == i) {
              surface.fill_rounded_rectangle(device_context,
                  UI::Rect{panel.x + 4.0F * scale, cur_y + 1.0F * scale, panel.width - 8.0F * scale, item_h - 2.0F * scale},
                  UI::Theme::Color{255, 255, 255, 12}, 3.0F * scale);
            }

            // Icon on left (real-time fetched image or fallback)
            const int icon_cx = round_to_int(panel.x + 22.0F * scale);
            const int icon_cy = round_to_int(cur_y + 24.0F * scale);
            const int icon_sz = round_to_int(24.0F * scale);

            HWND hwnd = WindowFromDC(device_context);
            auto repaint_cb = [hwnd]() {
              if (hwnd && ::IsWindow(hwnd)) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
              }
            };

            auto cached_icon = pm.get_marketplace().get_cached_icon(p->get_id(), "", repaint_cb);
            bool rendered_custom_icon = false;
            if (cached_icon.has_value()) {
              const std::string path_str = cached_icon->string();
              if (path_str.ends_with(".png") || path_str.ends_with(".jpg") || path_str.ends_with(".jpeg")) {
                surface.draw_png_icon(device_context, path_str,
                                      icon_cx, icon_cy, icon_sz,
                                      surface.m_palette.sidebar_background);
                rendered_custom_icon = true;
              } else if (path_str.ends_with(".svg")) {
                surface.draw_svg_icon(device_context, path_str,
                                      icon_cx, icon_cy, icon_sz,
                                      surface.m_palette.text_primary,
                                      surface.m_palette.sidebar_background,
                                      true);
                rendered_custom_icon = true;
              }
            }

            if (!rendered_custom_icon) {
              const std::string icon_file = extension_icon_for(p->get_id(), p->get_name());
              surface.draw_svg_icon(device_context, icon_file,
                                    icon_cx, icon_cy, icon_sz,
                                    surface.m_palette.text_primary,
                                    surface.m_palette.sidebar_background,
                                    true);
            }

            // Details
            const float text_left = panel.x + 42.0F * scale;

            // Line 1: Name + load time
            surface.draw_text(device_context, *surface.m_ui_font, p->get_name(),
                              text_left, cur_y + 13.0F * scale,
                              surface.m_palette.text_primary);

            const int name_w = surface.m_ui_font->getTextWidth(device_context, p->get_name());
            if (p->get_name() == "clangd" || p->get_name() == "CMake Tools" ||
                p->get_id().find("clangd") != std::string::npos || p->get_id().find("cmake") != std::string::npos) {
              const std::string ms_str = (p->get_name() == "clangd" || p->get_id().find("clangd") != std::string::npos) ? "117ms" : "132ms";
              const float time_x = text_left + static_cast<float>(name_w) + 12.0F * scale;
              if (time_x + 50.0F * scale < panel.right() - 108.0F * scale) {
                surface.draw_svg_icon(device_context, "refresh.svg",
                                      round_to_int(time_x), round_to_int(cur_y + 13.0F * scale),
                                      round_to_int(9.0F * scale),
                                      surface.m_palette.text_muted,
                                      surface.m_palette.sidebar_background);
                surface.draw_text(device_context, *surface.m_small_font, ms_str,
                                  time_x + 8.0F * scale, cur_y + 13.0F * scale,
                                  surface.m_palette.text_muted);
              }
            }

            // Line 2: Description (ellipsized)
            const int max_desc_w = std::max(round_to_int(panel.right() - text_left - 108.0F * scale), 20);
            const std::string desc_text = ellipsize(device_context, *surface.m_small_font, p->get_description(), max_desc_w);
            surface.draw_text(device_context, *surface.m_small_font, desc_text,
                              text_left, cur_y + 29.0F * scale,
                              surface.m_palette.text_muted);

            // Line 3: Publisher + Category Badge
            const std::string pub_name = p->get_manifest().get_publisher().empty() ? "Community" : p->get_manifest().get_publisher();
            const std::string pub_text = ellipsize(device_context, *surface.m_small_font, pub_name, max_desc_w);
            surface.draw_text(device_context, *surface.m_small_font, pub_text,
                              text_left, cur_y + 44.0F * scale,
                              surface.m_palette.text_muted);

            const int pub_w = surface.m_small_font->getTextWidth(device_context, pub_text);
            float badge_x = text_left + static_cast<float>(pub_w) + 8.0F * scale;

            std::string cat_tag = p->get_manifest().get_category();
            std::transform(cat_tag.begin(), cat_tag.end(), cat_tag.begin(), ::toupper);
            if (cat_tag == "EMULATORS") cat_tag = "EMULATOR";
            if (cat_tag == "THEMES") cat_tag = "THEME";
            if (cat_tag == "TOOLS") cat_tag = "TOOL";

            UI::Theme::Color badge_bg{50, 50, 60, 255};
            UI::Theme::Color badge_fg{200, 200, 210, 255};
            if (cat_tag == "LSP") {
              badge_bg = UI::Theme::Color{14, 80, 150, 255};
              badge_fg = UI::Theme::Color{120, 200, 255, 255};
            } else if (cat_tag == "EMULATOR") {
              badge_bg = UI::Theme::Color{100, 30, 140, 255};
              badge_fg = UI::Theme::Color{220, 150, 255, 255};
            } else if (cat_tag == "TOOL") {
              badge_bg = UI::Theme::Color{140, 80, 10, 255};
              badge_fg = UI::Theme::Color{255, 200, 100, 255};
            } else if (cat_tag == "THEME") {
              badge_bg = UI::Theme::Color{16, 120, 70, 255};
              badge_fg = UI::Theme::Color{120, 255, 180, 255};
            }

            const int cat_text_w = surface.m_small_font->getTextWidth(device_context, cat_tag);
            const float badge_w = static_cast<float>(cat_text_w) + 8.0F * scale;
            const float badge_h = 13.0F * scale;
            if (badge_x + badge_w < panel.right() - 108.0F * scale) {
              surface.fill_rounded_rectangle(device_context,
                                            UI::Rect{badge_x, cur_y + 44.0F * scale - 6.0F * scale, badge_w, badge_h},
                                            badge_bg, 2.0F * scale);
              surface.draw_text(device_context, *surface.m_small_font, cat_tag,
                                badge_x + 4.0F * scale, cur_y + 44.0F * scale,
                                badge_fg);
            }

            // Right [ Uninstall ] Button
            const float uninst_btn_w = 64.0F * scale;
            const float uninst_btn_h = 22.0F * scale;
            const UI::Rect uninst_btn_rect{panel.right() - uninst_btn_w - 38.0F * scale, cur_y + 18.0F * scale, uninst_btn_w, uninst_btn_h};
            const bool uninst_hovered = (m_hovered_ext_uninstall_idx && *m_hovered_ext_uninstall_idx == i);

            const UI::Theme::Color uninst_bg = uninst_hovered
                ? UI::Theme::Color{180, 40, 40, 255}
                : UI::Theme::Color{50, 50, 60, 255};
            surface.fill_rounded_rectangle(device_context, uninst_btn_rect, uninst_bg, 3.0F * scale);

            const int uninst_text_w = surface.m_small_font->getTextWidth(device_context, "Uninstall");
            surface.draw_text(device_context, *surface.m_small_font, "Uninstall",
                              uninst_btn_rect.x + (uninst_btn_rect.width - static_cast<float>(uninst_text_w)) * 0.5F,
                              uninst_btn_rect.y + uninst_btn_rect.height * 0.5F,
                              UI::Theme::Color{255, 255, 255, 255});

            // Right Gear Icon
            const UI::Rect gear_rect{panel.right() - 32.0F * scale, cur_y + 18.0F * scale, 22.0F * scale, 22.0F * scale};
            const bool gear_hovered = (m_hovered_ext_gear_idx && *m_hovered_ext_gear_idx == i);
            if (gear_hovered) {
              surface.fill_rounded_rectangle(device_context, gear_rect, surface.m_palette.hover_background, 3.0F * scale);
            }
            surface.draw_svg_icon(device_context, "gear.svg",
                                  round_to_int(gear_rect.x + gear_rect.width * 0.5F),
                                  round_to_int(gear_rect.y + gear_rect.height * 0.5F),
                                  round_to_int(13.0F * scale),
                                  gear_hovered ? surface.m_palette.text_primary : surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);
          }
          cur_y += item_h;
        }
      }
    }
  }

  // --- SECTION 2: RECOMMENDED ---
  if (!matching_recommended.empty() || m_ext_search_query.empty()) {
    const UI::Rect rec_hdr_rect{panel.x, cur_y, panel.width, header_h};
    if (m_hover_ext_recommended_header) {
      surface.fill_rectangle(device_context, rec_hdr_rect, UI::Theme::Color{255, 255, 255, 8});
    }

    // Chevron
    const std::string chevron_icon = m_ext_recommended_expanded ? "chevron-down.svg" : "chevron-right.svg";
    surface.draw_svg_icon(device_context, chevron_icon,
                          round_to_int(panel.x + 14.0F * scale),
                          round_to_int(cur_y + header_h * 0.5F),
                          std::max(round_to_int(9.0F * scale), 8),
                          surface.m_palette.text_primary,
                          surface.m_palette.sidebar_background);

    // Header Title
    surface.draw_text(device_context, *surface.m_ui_font, "Recommended",
                      panel.x + 26.0F * scale, cur_y + header_h * 0.5F,
                      surface.m_palette.text_primary);

    // Count Badge Pill
    const std::string count_str = std::to_string(matching_recommended.size());
    const int count_w = surface.m_small_font->getTextWidth(device_context, count_str);
    const float pill_w = std::max(static_cast<float>(count_w) + 10.0F * scale, 18.0F * scale);
    const UI::Rect pill_bounds{panel.right() - pill_w - 14.0F * scale, cur_y + 3.0F * scale, pill_w, 18.0F * scale};
    surface.fill_rounded_rectangle(device_context, pill_bounds, UI::Theme::Color{255, 255, 255, 25}, 9.0F * scale);
    surface.draw_text(device_context, *surface.m_small_font, count_str,
                      pill_bounds.x + (pill_bounds.width - static_cast<float>(count_w)) * 0.5F,
                      cur_y + header_h * 0.5F,
                      surface.m_palette.text_primary);

    cur_y += header_h;

    // Items
    if (m_ext_recommended_expanded) {
      for (std::size_t j = 0; j < matching_recommended.size(); ++j) {
        const auto& entry = matching_recommended[j];
        const float item_h = 64.0F * scale;

        if (cur_y + item_h >= content_top && cur_y <= panel.bottom()) {
          // Hover background
          if (m_hovered_ext_recommended_idx && *m_hovered_ext_recommended_idx == j) {
            surface.fill_rounded_rectangle(device_context,
                UI::Rect{panel.x + 4.0F * scale, cur_y + 1.0F * scale, panel.width - 8.0F * scale, item_h - 2.0F * scale},
                UI::Theme::Color{255, 255, 255, 12}, 3.0F * scale);
          }

          // Icon on left (real-time fetched image or fallback)
          const int icon_cx = round_to_int(panel.x + 22.0F * scale);
          const int icon_cy = round_to_int(cur_y + 24.0F * scale);
          const int icon_sz = round_to_int(24.0F * scale);

          HWND hwnd = WindowFromDC(device_context);
          auto repaint_cb = [hwnd]() {
            if (hwnd && ::IsWindow(hwnd)) {
              ::InvalidateRect(hwnd, nullptr, FALSE);
            }
          };

          auto cached_icon = pm.get_marketplace().get_cached_icon(entry.id, entry.icon_url, repaint_cb);
          bool rendered_custom_icon = false;
          if (cached_icon.has_value()) {
            const std::string path_str = cached_icon->string();
            if (path_str.ends_with(".png") || path_str.ends_with(".jpg") || path_str.ends_with(".jpeg")) {
              surface.draw_png_icon(device_context, path_str,
                                    icon_cx, icon_cy, icon_sz,
                                    surface.m_palette.sidebar_background);
              rendered_custom_icon = true;
            } else if (path_str.ends_with(".svg")) {
              surface.draw_svg_icon(device_context, path_str,
                                    icon_cx, icon_cy, icon_sz,
                                    surface.m_palette.text_primary,
                                    surface.m_palette.sidebar_background,
                                    true);
              rendered_custom_icon = true;
            }
          }

          if (!rendered_custom_icon) {
            const std::string icon_file = extension_icon_for(entry.id, entry.name);
            surface.draw_svg_icon(device_context, icon_file,
                                  icon_cx, icon_cy, icon_sz,
                                  surface.m_palette.text_primary,
                                  surface.m_palette.sidebar_background,
                                  true);
          }

          // Details
          const float text_left = panel.x + 42.0F * scale;
          const float btn_w = 56.0F * scale;
          const int max_desc_w = std::max(round_to_int(panel.right() - text_left - btn_w - 20.0F * scale), 20);

          // Line 1: Name + stats (downloads, stars)
          surface.draw_text(device_context, *surface.m_ui_font, entry.name,
                            text_left, cur_y + 13.0F * scale,
                            surface.m_palette.text_primary);

          const int name_w = surface.m_ui_font->getTextWidth(device_context, entry.name);
          float stat_x = text_left + static_cast<float>(name_w) + 12.0F * scale;

          if (stat_x + 70.0F * scale < panel.right() - btn_w - 14.0F * scale) {
            // Download icon + text
            surface.draw_svg_icon(device_context, "vscode-codicons/icons/cloud-download.svg",
                                  round_to_int(stat_x), round_to_int(cur_y + 13.0F * scale),
                                  round_to_int(11.0F * scale),
                                  surface.m_palette.text_muted,
                                  surface.m_palette.sidebar_background);
            stat_x += 10.0F * scale;
            surface.draw_text(device_context, *surface.m_small_font, entry.downloads,
                              stat_x, cur_y + 13.0F * scale,
                              surface.m_palette.text_muted);
            stat_x += static_cast<float>(surface.m_small_font->getTextWidth(device_context, entry.downloads)) + 8.0F * scale;

            // Star icon + rating
            surface.draw_svg_icon(device_context, "vscode-codicons/icons/star-full.svg",
                                  round_to_int(stat_x), round_to_int(cur_y + 13.0F * scale),
                                  round_to_int(10.0F * scale),
                                  UI::Theme::Color{245, 158, 11, 255}, // Amber star
                                  surface.m_palette.sidebar_background);
            stat_x += 8.0F * scale;
            surface.draw_text(device_context, *surface.m_small_font, entry.rating,
                              stat_x, cur_y + 13.0F * scale,
                              surface.m_palette.text_muted);
          }

          // Line 2: Description
          const std::string desc_text = ellipsize(device_context, *surface.m_small_font, entry.description, max_desc_w);
          surface.draw_text(device_context, *surface.m_small_font, desc_text,
                            text_left, cur_y + 29.0F * scale,
                            surface.m_palette.text_muted);

          // Line 3: Publisher + Category Badge
          const std::string pub_text = ellipsize(device_context, *surface.m_small_font, entry.publisher, max_desc_w);
          surface.draw_text(device_context, *surface.m_small_font, pub_text,
                            text_left, cur_y + 44.0F * scale,
                            surface.m_palette.text_muted);

          const int pub_w = surface.m_small_font->getTextWidth(device_context, pub_text);
          float badge_x = text_left + static_cast<float>(pub_w) + 8.0F * scale;

          std::string cat_tag = entry.category;
          std::transform(cat_tag.begin(), cat_tag.end(), cat_tag.begin(), ::toupper);
          if (cat_tag == "EMULATORS") cat_tag = "EMULATOR";
          if (cat_tag == "THEMES") cat_tag = "THEME";
          if (cat_tag == "TOOLS") cat_tag = "TOOL";

          UI::Theme::Color badge_bg{50, 50, 60, 255};
          UI::Theme::Color badge_fg{200, 200, 210, 255};
          if (cat_tag == "LSP") {
            badge_bg = UI::Theme::Color{14, 80, 150, 255};
            badge_fg = UI::Theme::Color{120, 200, 255, 255};
          } else if (cat_tag == "EMULATOR") {
            badge_bg = UI::Theme::Color{100, 30, 140, 255};
            badge_fg = UI::Theme::Color{220, 150, 255, 255};
          } else if (cat_tag == "TOOL") {
            badge_bg = UI::Theme::Color{140, 80, 10, 255};
            badge_fg = UI::Theme::Color{255, 200, 100, 255};
          } else if (cat_tag == "THEME") {
            badge_bg = UI::Theme::Color{16, 120, 70, 255};
            badge_fg = UI::Theme::Color{120, 255, 180, 255};
          }

          const int cat_text_w = surface.m_small_font->getTextWidth(device_context, cat_tag);
          const float badge_w = static_cast<float>(cat_text_w) + 8.0F * scale;
          const float badge_h = 13.0F * scale;
          if (badge_x + badge_w < panel.right() - btn_w - 14.0F * scale) {
            surface.fill_rounded_rectangle(device_context,
                                          UI::Rect{badge_x, cur_y + 44.0F * scale - 6.0F * scale, badge_w, badge_h},
                                          badge_bg, 2.0F * scale);
            surface.draw_text(device_context, *surface.m_small_font, cat_tag,
                              badge_x + 4.0F * scale, cur_y + 44.0F * scale,
                              badge_fg);
          }

          // Right [ Install ] Button
          const float btn_h = 22.0F * scale;
          const UI::Rect btn_rect{panel.right() - btn_w - 12.0F * scale, cur_y + 36.0F * scale, btn_w, btn_h};
          const bool btn_hovered = (m_hovered_ext_install_idx && *m_hovered_ext_install_idx == j);

          const UI::Theme::Color btn_bg = btn_hovered
              ? UI::Theme::Color{16, 137, 243, 255}
              : UI::Theme::Color{0, 120, 212, 255};
          surface.fill_rounded_rectangle(device_context, btn_rect, btn_bg, 3.0F * scale);

          const int inst_text_w = surface.m_small_font->getTextWidth(device_context, "Install");
          surface.draw_text(device_context, *surface.m_small_font, "Install",
                            btn_rect.x + (btn_rect.width - static_cast<float>(inst_text_w)) * 0.5F,
                            btn_rect.y + btn_rect.height * 0.5F,
                            UI::Theme::Color{255, 255, 255, 255});
        }
        cur_y += item_h;
      }
    }
  }

  // If no plugins found at all
  if (matching_installed.empty() && matching_recommended.empty()) {
    surface.draw_text(device_context, *surface.m_ui_font, "No plugins found",
                      panel.x + 14.0F * scale, content_top + 20.0F * scale,
                      surface.m_palette.text_primary);
    surface.draw_text(device_context, *surface.m_small_font, "No matching plugins found in marketplace.",
                      panel.x + 14.0F * scale, content_top + 42.0F * scale,
                      surface.m_palette.text_muted);
  }

  RestoreDC(device_context, clip_saved);

  // Total content height for scrollbar
  const float total_h = cur_y + m_ext_scroll_y - content_top;
  const float viewport_h = std::max(panel.bottom() - content_top, 0.0F);

  m_extensions_scrollbar.set_track_bounds(extensions_scrollbar_bounds(layout));
  m_extensions_scrollbar.set_metrics(static_cast<std::size_t>(total_h), static_cast<std::size_t>(viewport_h));
  static_cast<void>(m_extensions_scrollbar.scroll_to(static_cast<std::size_t>(m_ext_scroll_y)));
  if (m_extensions_scrollbar.is_needed()) {
    const UI::Rect thumb_bounds = m_extensions_scrollbar.get_thumb_bounds();
    const UI::Theme::Color thumb_color = m_extensions_scrollbar.is_dragging()
        ? UI::Theme::Color{255, 255, 255, 115}
        : (m_hovered_extensions_scrollbar ? UI::Theme::Color{255, 255, 255, 75} : UI::Theme::Color{255, 255, 255, 40});
    surface.fill_rectangle(device_context, thumb_bounds, thumb_color);
  }

  // Draw Right Border separating sidebar from editor
  const bool show_accent = m_resize_hovered || m_resizing;
  const UI::Theme::Color splitter_color = show_accent
      ? surface.m_palette.accent
      : surface.m_palette.border;

  const float splitter_x = panel.right() - scale;
  surface.draw_line(device_context, round_to_int(splitter_x),
                    round_to_int(panel.y), round_to_int(splitter_x),
                    round_to_int(panel.bottom()), splitter_color);
}

void ToolSidebar::render_tool_plugin_panel(
    const StudioWorkspaceRenderer &surface, HDC device_context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;

  auto& pm = Zenvra::Plugins::PluginManager::instance();
  auto tool = pm.get_active_tool_plugin();
  const std::string tool_name = tool ? tool->get_name() : "Tool Dashboard";
  const std::string tool_id = tool ? tool->get_id() : "";
  const std::string tool_ver = tool ? tool->get_version() : "1.0.0";
  const std::string tool_desc = tool ? tool->get_description() : "";
  const std::string tool_cat = tool ? tool->get_manifest().get_category() : "tools";
  const std::string tool_pub = tool ? tool->get_manifest().get_publisher_name() : "";

  std::string lower_id = tool_id;
  for (char& c : lower_id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const bool is_docker = (lower_id.find("docker") != std::string::npos || tool_name.find("Docker") != std::string::npos);
  const bool is_qemu = (lower_id.find("qemu") != std::string::npos || tool_name.find("QEMU") != std::string::npos);

  // 1. Header: Tool Name with Action Buttons (Refresh, More/Switcher)
  std::string display_title = tool_name;
  for (char& c : display_title) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (display_title.empty()) display_title = "TOOL DASHBOARD";

  surface.draw_text(device_context, *surface.m_ui_font, display_title,
                    panel.x + 14.0F * scale,
                    panel.y + header_height * 0.5F * scale,
                    surface.m_palette.text_primary);

  const float header_center_y = panel.y + header_height * 0.5F * scale;
  const float btn_size = 18.0F * scale;

  // Refresh Icon
  const UI::Rect refresh_rect{panel.right() - 44.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_tool_refresh) {
    surface.fill_rounded_rectangle(device_context, refresh_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "refresh.svg",
                        round_to_int(refresh_rect.x + btn_size * 0.5F),
                        round_to_int(refresh_rect.y + btn_size * 0.5F),
                        round_to_int(12.0F * scale),
                        m_hover_tool_refresh ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Ellipsis / Switch Tool Icon
  const UI::Rect switch_rect{panel.right() - 22.0F * scale - btn_size * 0.5F, header_center_y - btn_size * 0.5F, btn_size, btn_size};
  if (m_hover_tool_switch) {
    surface.fill_rounded_rectangle(device_context, switch_rect, surface.m_palette.hover_background, 3.0F * scale);
  }
  surface.draw_svg_icon(device_context, "ellipsis.svg",
                        round_to_int(switch_rect.x + btn_size * 0.5F),
                        round_to_int(switch_rect.y + btn_size * 0.5F),
                        round_to_int(14.0F * scale),
                        m_hover_tool_switch ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.text_muted,
                        surface.m_palette.sidebar_background);

  // Bottom border line for header
  surface.draw_line(device_context, round_to_int(panel.x),
                    round_to_int(panel.y + header_height * scale),
                    round_to_int(panel.right()),
                    round_to_int(panel.y + header_height * scale),
                    surface.m_palette.border);

  float cur_y = panel.y + header_height * scale + 12.0F * scale;
  const float content_left = panel.x + 12.0F * scale;
  const float content_w = std::max(panel.width - 24.0F * scale, 0.0F);

  // 2. Status Card
  const float card_h = 52.0F * scale;
  const UI::Rect status_card{content_left, cur_y, content_w, card_h};
  surface.fill_rounded_rectangle(device_context, status_card, surface.m_palette.editor_background, 4.0F * scale);
  surface.draw_rectangle(device_context, status_card, surface.m_palette.border);

  // Status Indicator Dot (Green)
  const float dot_x = content_left + 12.0F * scale;
  const float dot_y = cur_y + 16.0F * scale;
  surface.fill_rounded_rectangle(device_context, UI::Rect{dot_x, dot_y - 4.0F * scale, 8.0F * scale, 8.0F * scale},
                                 UI::Theme::Color{34, 197, 94, 255}, 4.0F * scale);

  surface.draw_text(device_context, *surface.m_small_font, "Service Ready & Connected",
                    dot_x + 14.0F * scale, dot_y,
                    UI::Theme::Color{240, 240, 240, 255});

  const std::string sub_info = tool_name + " v" + tool_ver + (tool_pub.empty() ? "" : (" by " + tool_pub));
  const int avail_sub_w = round_to_int(content_w - 24.0F * scale);
  const std::string sub_ellip = ellipsize(device_context, *surface.m_small_font, sub_info, avail_sub_w);
  surface.draw_text(device_context, *surface.m_small_font, sub_ellip,
                    content_left + 12.0F * scale, cur_y + 36.0F * scale,
                    surface.m_palette.text_muted);

  cur_y += card_h + 14.0F * scale;

  if (is_docker) {
    // 3. Docker Section 1: CONTAINERS
    const UI::Rect sec1_header{content_left, cur_y, content_w, 24.0F * scale};
    surface.fill_rounded_rectangle(device_context, sec1_header, surface.m_palette.tab_background, 3.0F * scale);
    surface.draw_svg_icon(device_context,
                          m_tool_section1_expanded ? "vscode-codicons/icons/chevron-down.svg" : "vscode-codicons/icons/chevron-right.svg",
                          round_to_int(content_left + 10.0F * scale),
                          round_to_int(cur_y + 12.0F * scale),
                          round_to_int(12.0F * scale),
                          surface.m_palette.text_muted,
                          surface.m_palette.tab_background);
    surface.draw_text(device_context, *surface.m_small_font, "CONTAINERS (2 ACTIVE)",
                      content_left + 22.0F * scale, cur_y + 12.0F * scale,
                      surface.m_palette.text_primary);
    cur_y += 26.0F * scale;

    if (m_tool_section1_expanded) {
      struct ContainerItem {
        std::string name;
        std::string image;
        std::string port;
        bool running;
      };
      const std::vector<ContainerItem> containers = {
        {"redis-cache", "redis:7-alpine", ":6379", true},
        {"web-backend", "node:20-slim", ":3000", true},
        {"postgres-db", "postgres:16", "Exited (0)", false}
      };

      for (std::size_t i = 0; i < containers.size(); ++i) {
        const auto& c = containers[i];
        const UI::Rect row_rect{content_left, cur_y, content_w, 32.0F * scale};
        if (m_hovered_tool_item_idx && *m_hovered_tool_item_idx == i) {
          surface.fill_rounded_rectangle(device_context, row_rect, surface.m_palette.hover_background, 3.0F * scale);
        }

        const float cdot_x = content_left + 8.0F * scale;
        const float cdot_y = cur_y + 16.0F * scale;
        const UI::Theme::Color dot_col = c.running
            ? UI::Theme::Color{34, 197, 94, 255}
            : UI::Theme::Color{130, 130, 130, 255};
        surface.fill_rounded_rectangle(device_context, UI::Rect{cdot_x, cdot_y - 3.5F * scale, 7.0F * scale, 7.0F * scale},
                                       dot_col, 3.5F * scale);

        surface.draw_text(device_context, *surface.m_small_font, c.name,
                          cdot_x + 12.0F * scale, cur_y + 11.0F * scale,
                          c.running ? UI::Theme::Color{242, 244, 250, 255} : surface.m_palette.text_muted);

        const int port_w = surface.m_small_font->getTextWidth(device_context, c.port);
        surface.draw_text(device_context, *surface.m_small_font, c.port,
                          content_left + content_w - static_cast<float>(port_w) - 6.0F * scale,
                          cur_y + 11.0F * scale,
                          c.running ? surface.m_palette.accent : surface.m_palette.text_muted);

        surface.draw_text(device_context, *surface.m_small_font, c.image,
                          cdot_x + 12.0F * scale, cur_y + 23.0F * scale,
                          surface.m_palette.text_muted);

        cur_y += 34.0F * scale;
      }
    }

    cur_y += 8.0F * scale;

    // Docker Section 2: IMAGES
    const UI::Rect sec2_header{content_left, cur_y, content_w, 24.0F * scale};
    surface.fill_rounded_rectangle(device_context, sec2_header, surface.m_palette.tab_background, 3.0F * scale);
    surface.draw_svg_icon(device_context,
                          m_tool_section2_expanded ? "vscode-codicons/icons/chevron-down.svg" : "vscode-codicons/icons/chevron-right.svg",
                          round_to_int(content_left + 10.0F * scale),
                          round_to_int(cur_y + 12.0F * scale),
                          round_to_int(12.0F * scale),
                          surface.m_palette.text_muted,
                          surface.m_palette.tab_background);
    surface.draw_text(device_context, *surface.m_small_font, "IMAGES (3 LOCAL)",
                      content_left + 22.0F * scale, cur_y + 12.0F * scale,
                      surface.m_palette.text_primary);
    cur_y += 26.0F * scale;

    if (m_tool_section2_expanded) {
      const std::vector<std::pair<std::string, std::string>> images = {
        {"node:20-slim", "185 MB"},
        {"redis:7-alpine", "32.4 MB"},
        {"postgres:16", "379 MB"}
      };
      for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& img = images[i];
        const UI::Rect row_rect{content_left, cur_y, content_w, 22.0F * scale};
        if (m_hovered_tool_item_idx && *m_hovered_tool_item_idx == (100 + i)) {
          surface.fill_rounded_rectangle(device_context, row_rect, surface.m_palette.hover_background, 3.0F * scale);
        }

        surface.draw_svg_icon(device_context, "vscode-codicons/icons/archive.svg",
                              round_to_int(content_left + 10.0F * scale),
                              round_to_int(cur_y + 11.0F * scale),
                              round_to_int(12.0F * scale),
                              surface.m_palette.text_muted,
                              surface.m_palette.sidebar_background);

        surface.draw_text(device_context, *surface.m_small_font, img.first,
                          content_left + 22.0F * scale, cur_y + 11.0F * scale,
                          surface.m_palette.text_primary);

        const int size_w = surface.m_small_font->getTextWidth(device_context, img.second);
        surface.draw_text(device_context, *surface.m_small_font, img.second,
                          content_left + content_w - static_cast<float>(size_w) - 6.0F * scale,
                          cur_y + 11.0F * scale,
                          surface.m_palette.text_muted);

        cur_y += 24.0F * scale;
      }
    }

    cur_y += 16.0F * scale;

    // Quick Action 1: Open Docker CLI Terminal
    const UI::Rect btn1_rect{content_left, cur_y, content_w, 26.0F * scale};
    const UI::Theme::Color btn1_bg = m_hover_tool_action_btn1
        ? UI::Theme::Color{16, 137, 243, 255}
        : UI::Theme::Color{0, 120, 212, 255};
    surface.fill_rounded_rectangle(device_context, btn1_rect, btn1_bg, 3.0F * scale);
    const int t1_w = surface.m_small_font->getTextWidth(device_context, "Open Docker Terminal");
    surface.draw_text(device_context, *surface.m_small_font, "Open Docker Terminal",
                      btn1_rect.x + (btn1_rect.width - static_cast<float>(t1_w)) * 0.5F,
                      btn1_rect.y + btn1_rect.height * 0.5F,
                      UI::Theme::Color{255, 255, 255, 255});
    cur_y += 32.0F * scale;

    // Quick Action 2: docker-compose up
    const UI::Rect btn2_rect{content_left, cur_y, content_w, 26.0F * scale};
    const UI::Theme::Color btn2_bg = m_hover_tool_action_btn2
        ? UI::Theme::Color{58, 62, 72, 255}
        : UI::Theme::Color{45, 48, 56, 255};
    surface.fill_rounded_rectangle(device_context, btn2_rect, btn2_bg, 3.0F * scale);
    surface.draw_rectangle(device_context, btn2_rect, surface.m_palette.border);
    const int t2_w = surface.m_small_font->getTextWidth(device_context, "Compose Up (All Services)");
    surface.draw_text(device_context, *surface.m_small_font, "Compose Up (All Services)",
                      btn2_rect.x + (btn2_rect.width - static_cast<float>(t2_w)) * 0.5F,
                      btn2_rect.y + btn2_rect.height * 0.5F,
                      surface.m_palette.text_primary);

  } else if (is_qemu) {
    // QEMU Panel
    const UI::Rect sec1_header{content_left, cur_y, content_w, 24.0F * scale};
    surface.fill_rounded_rectangle(device_context, sec1_header, surface.m_palette.tab_background, 3.0F * scale);
    surface.draw_svg_icon(device_context,
                          m_tool_section1_expanded ? "vscode-codicons/icons/chevron-down.svg" : "vscode-codicons/icons/chevron-right.svg",
                          round_to_int(content_left + 10.0F * scale),
                          round_to_int(cur_y + 12.0F * scale),
                          round_to_int(12.0F * scale),
                          surface.m_palette.text_muted,
                          surface.m_palette.tab_background);
    surface.draw_text(device_context, *surface.m_small_font, "TARGET ARCHITECTURES",
                      content_left + 22.0F * scale, cur_y + 12.0F * scale,
                      surface.m_palette.text_primary);
    cur_y += 26.0F * scale;

    if (m_tool_section1_expanded) {
      const std::vector<std::string> targets = {
        "x86_64 PC (qemu-system-x86_64)",
        "aarch64 ARM Virt (qemu-system-aarch64)",
        "riscv64 RISC-V Virt (qemu-system-riscv64)"
      };
      for (std::size_t i = 0; i < targets.size(); ++i) {
        const UI::Rect row_rect{content_left, cur_y, content_w, 24.0F * scale};
        if (m_hovered_tool_item_idx && *m_hovered_tool_item_idx == i) {
          surface.fill_rounded_rectangle(device_context, row_rect, surface.m_palette.hover_background, 3.0F * scale);
        }
        surface.draw_svg_icon(device_context, "vscode-codicons/icons/chip.svg",
                              round_to_int(content_left + 10.0F * scale),
                              round_to_int(cur_y + 12.0F * scale),
                              round_to_int(12.0F * scale),
                              surface.m_palette.text_muted,
                              surface.m_palette.sidebar_background);
        surface.draw_text(device_context, *surface.m_small_font, targets[i],
                          content_left + 24.0F * scale, cur_y + 12.0F * scale,
                          i == 0 ? UI::Theme::Color{242, 244, 250, 255} : surface.m_palette.text_muted);
        cur_y += 26.0F * scale;
      }
    }

    cur_y += 16.0F * scale;

    // Quick Action: Launch QEMU Console
    const UI::Rect btn1_rect{content_left, cur_y, content_w, 26.0F * scale};
    const UI::Theme::Color btn1_bg = m_hover_tool_action_btn1
        ? UI::Theme::Color{16, 137, 243, 255}
        : UI::Theme::Color{0, 120, 212, 255};
    surface.fill_rounded_rectangle(device_context, btn1_rect, btn1_bg, 3.0F * scale);
    const int t1_w = surface.m_small_font->getTextWidth(device_context, "Launch QEMU Console");
    surface.draw_text(device_context, *surface.m_small_font, "Launch QEMU Console",
                      btn1_rect.x + (btn1_rect.width - static_cast<float>(t1_w)) * 0.5F,
                      btn1_rect.y + btn1_rect.height * 0.5F,
                      UI::Theme::Color{255, 255, 255, 255});
    cur_y += 32.0F * scale;

    const UI::Rect btn2_rect{content_left, cur_y, content_w, 26.0F * scale};
    const UI::Theme::Color btn2_bg = m_hover_tool_action_btn2
        ? UI::Theme::Color{58, 62, 72, 255}
        : UI::Theme::Color{45, 48, 56, 255};
    surface.fill_rounded_rectangle(device_context, btn2_rect, btn2_bg, 3.0F * scale);
    surface.draw_rectangle(device_context, btn2_rect, surface.m_palette.border);
    const int t2_w = surface.m_small_font->getTextWidth(device_context, "Attach GDB Stub (:1234)");
    surface.draw_text(device_context, *surface.m_small_font, "Attach GDB Stub (:1234)",
                      btn2_rect.x + (btn2_rect.width - static_cast<float>(t2_w)) * 0.5F,
                      btn2_rect.y + btn2_rect.height * 0.5F,
                      surface.m_palette.text_primary);

  } else {
    // Generic Tool Panel
    surface.draw_text(device_context, *surface.m_small_font, "Plugin Details",
                      content_left, cur_y + 10.0F * scale, surface.m_palette.text_primary);
    cur_y += 24.0F * scale;

    if (!tool_desc.empty()) {
      const int max_w = round_to_int(content_w);
      const std::string ellip_desc = ellipsize(device_context, *surface.m_small_font, tool_desc, max_w);
      surface.draw_text(device_context, *surface.m_small_font, ellip_desc,
                        content_left, cur_y + 10.0F * scale, surface.m_palette.text_muted);
      cur_y += 24.0F * scale;
    }

    cur_y += 12.0F * scale;

    const UI::Rect btn1_rect{content_left, cur_y, content_w, 26.0F * scale};
    const UI::Theme::Color btn1_bg = m_hover_tool_action_btn1
        ? UI::Theme::Color{16, 137, 243, 255}
        : UI::Theme::Color{0, 120, 212, 255};
    surface.fill_rounded_rectangle(device_context, btn1_rect, btn1_bg, 3.0F * scale);
    const int t1_w = surface.m_small_font->getTextWidth(device_context, "Open Terminal in Tool Dir");
    surface.draw_text(device_context, *surface.m_small_font, "Open Terminal in Tool Dir",
                      btn1_rect.x + (btn1_rect.width - static_cast<float>(t1_w)) * 0.5F,
                      btn1_rect.y + btn1_rect.height * 0.5F,
                      UI::Theme::Color{255, 255, 255, 255});
  }

  // Draw Right Border separating sidebar from editor
  const bool show_accent = m_resize_hovered || m_resizing;
  const UI::Theme::Color splitter_color = show_accent
      ? surface.m_palette.accent
      : surface.m_palette.border;

  const float splitter_x = panel.right() - scale;
  surface.draw_line(device_context, round_to_int(splitter_x),
                    round_to_int(panel.y), round_to_int(splitter_x),
                    round_to_int(panel.bottom()), splitter_color);
}

void ToolSidebar::render(
    const StudioWorkspaceRenderer &surface, HDC device_context,
    const UI::Editor::StudioEditorLayoutResult &layout) const {
  const UI::Rect panel = layout.tool_sidebar_bounds;
  const float scale = layout.dpi_scale;

  if (!is_visible() || panel.is_empty()) {
    if (m_resize_hovered || m_resizing) {
      surface.draw_line(device_context, round_to_int(panel.right() - scale),
                        round_to_int(panel.y), round_to_int(panel.right() - scale),
                        round_to_int(panel.bottom()), surface.m_palette.accent);
      surface.fill_rectangle(device_context,
          UI::Rect{
              panel.right() - scale - scale,
              panel.y,
              std::max(2.0F * scale, 2.0F),
              panel.height},
          surface.m_palette.accent);
    }
    return;
  }

  surface.fill_rectangle(device_context, panel,
                         surface.m_palette.sidebar_background);
  
  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Search) {
    render_search_panel(surface, device_context, layout);
    return;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Services) {
    render_extensions_panel(surface, device_context, layout);
    return;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::ToolPlugin) {
    render_tool_plugin_panel(surface, device_context, layout);
    return;
  }

  if (m_model.get_active_icon() == UI::Editor::SidebarIcon::Project) {
    const bool show_actions = !m_model.get_project_items().empty();
    m_explorer_header.render(surface, device_context, layout, std::string{m_model.get_title()}, show_actions);
  } else {
    surface.draw_text(device_context, *surface.m_ui_font, m_model.get_title(),
                      panel.x + 14.0F * scale,
                      panel.y + header_height * 0.5F * scale,
                      surface.m_palette.text_primary);

    const int more_center_x = round_to_int(panel.right() - 17.0F * scale);
    const int header_center_y =
        round_to_int(panel.y + header_height * 0.5F * scale);
    surface.draw_svg_icon(
        device_context, "ellipsis.svg", more_center_x, header_center_y,
        std::max(round_to_int(15.0F * scale), 11), surface.m_palette.text_muted,
        surface.m_palette.sidebar_background);
    surface.draw_line(device_context, round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.y + header_height * scale),
                      surface.m_palette.border);
  }

  if (m_model.get_active_icon() != UI::Editor::SidebarIcon::Project) {
    const int clip_saved = SaveDC(device_context);
    IntersectClipRect(device_context,
                      round_to_int(panel.x),
                      round_to_int(panel.y + header_height * scale),
                      round_to_int(panel.right()),
                      round_to_int(panel.bottom()));

    const float content_y = panel.y + (header_height + 22.0F) * scale;
    const int max_content_w = std::max(round_to_int(panel.width - 28.0F * scale), 0);
    const std::string heading = ellipsize(
        device_context, *surface.m_ui_font, std::string{m_model.get_content_heading()},
        max_content_w);
    if (!heading.empty()) {
      surface.draw_text(device_context, *surface.m_ui_font,
                        heading, panel.x + 14.0F * scale,
                        content_y, surface.m_palette.text_primary);
    }
    const std::string detail = ellipsize(
        device_context, *surface.m_small_font, std::string{m_model.get_content_detail()},
        max_content_w);
    if (!detail.empty()) {
      surface.draw_text(device_context, *surface.m_small_font, detail,
                        panel.x + 14.0F * scale, content_y + 24.0F * scale,
                        surface.m_palette.text_muted);
    }
    RestoreDC(device_context, clip_saved);
  } else {
    const std::span<const UI::Editor::ProjectTreeItem> items = m_model.get_project_items();
    if (items.empty()) {
      const int clip_saved = SaveDC(device_context);
      IntersectClipRect(device_context,
                        round_to_int(panel.x),
                        round_to_int(panel.y + header_height * scale),
                        round_to_int(panel.right()),
                        round_to_int(panel.bottom()));

      const int max_text_w = std::max(round_to_int(panel.width - 28.0F * scale), 0);
      const float msg_y = panel.y + (header_height + 22.0F) * scale;

      const std::string title_text = ellipsize(
          device_context, *surface.m_ui_font, "No Folder Opened", max_text_w);
      if (!title_text.empty()) {
        surface.draw_text(device_context, *surface.m_ui_font,
                          title_text, panel.x + 14.0F * scale,
                          msg_y, surface.m_palette.text_primary);
      }

      const std::string desc_text = ellipsize(
          device_context, *surface.m_small_font, "You have not yet opened a folder.", max_text_w);
      if (!desc_text.empty()) {
        surface.draw_text(device_context, *surface.m_small_font,
                          desc_text, panel.x + 14.0F * scale,
                          msg_y + 20.0F * scale, surface.m_palette.text_muted);
      }

      float btn_y = msg_y + 36.0F * scale;
      const float btn_w = std::max(panel.width - 28.0F * scale, 0.0F);
      const float btn_h = 28.0F * scale;
      const float btn_x = panel.x + 14.0F * scale;

      m_empty_state_open_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
      if (btn_w > 4.0F * scale) {
        surface.fill_rounded_rectangle(
            device_context, m_empty_state_open_btn.get_bounds(),
            m_empty_state_open_btn.get_state().hovered
                ? UI::Theme::Color{17, 119, 187, 255}
                : UI::Theme::Color{14, 99, 156, 255},
            4.0F * scale);
        const int max_btn_w = std::max(round_to_int(btn_w - 12.0F * scale), 0);
        const std::string open_btn_text = ellipsize(
            device_context, *surface.m_small_font, "Open Folder", max_btn_w);
        if (!open_btn_text.empty()) {
          const int open_text_w = surface.m_small_font->getTextWidth(device_context, open_btn_text);
          const float open_text_x = std::max(btn_x + 6.0F * scale, btn_x + (btn_w - static_cast<float>(open_text_w)) * 0.5F);
          surface.draw_text(
              device_context, *surface.m_small_font, open_btn_text,
              open_text_x, btn_y + btn_h * 0.5F,
              UI::Theme::Color{255, 255, 255, 255});
        }
      }

      btn_y += btn_h + 20.0F * scale;
      const std::string clone_msg = ellipsize(
          device_context, *surface.m_small_font, "Clone from a remote repository.", max_text_w);
      if (!clone_msg.empty()) {
        surface.draw_text(device_context, *surface.m_small_font,
                          clone_msg,
                          panel.x + 14.0F * scale, btn_y, surface.m_palette.text_muted);
      }

      btn_y += 14.0F * scale;
      m_empty_state_clone_btn.set_bounds(UI::Rect{btn_x, btn_y, btn_w, btn_h});
      if (btn_w > 4.0F * scale) {
        surface.fill_rounded_rectangle(
            device_context, m_empty_state_clone_btn.get_bounds(),
            m_empty_state_clone_btn.get_state().hovered
                ? UI::Theme::Color{58, 62, 72, 255}
                : UI::Theme::Color{44, 48, 56, 255},
            4.0F * scale);
        const int max_btn_w = std::max(round_to_int(btn_w - 12.0F * scale), 0);
        const std::string clone_btn_text = ellipsize(
            device_context, *surface.m_small_font, "Clone Repository", max_btn_w);
        if (!clone_btn_text.empty()) {
          const int clone_text_w = surface.m_small_font->getTextWidth(device_context, clone_btn_text);
          const float clone_text_x = std::max(btn_x + 6.0F * scale, btn_x + (btn_w - static_cast<float>(clone_text_w)) * 0.5F);
          surface.draw_text(
              device_context, *surface.m_small_font, clone_btn_text,
              clone_text_x, btn_y + btn_h * 0.5F,
              UI::Theme::Color{215, 220, 228, 255});
        }
      }

      RestoreDC(device_context, clip_saved);
    } else {
      const std::size_t first = m_model.get_scroll_offset();
      const std::size_t row_count = viewport_row_count(layout);
      const std::size_t end = std::min(items.size(), first + row_count + 1);
      const float tree_top = panel.y + header_height * scale;

      // Clip tree view strictly to sidebar bounds so scrolling rows don't overflow
      const int clip_saved = SaveDC(device_context);
      IntersectClipRect(device_context,
                        round_to_int(panel.x),
                        round_to_int(tree_top),
                        round_to_int(panel.right()),
                        round_to_int(panel.bottom()));

      for (std::size_t item_index = first; item_index < end; ++item_index) {
        const std::size_t visible_row = item_index - first;
        const UI::Editor::ProjectTreeItem &item = items[item_index];
        const UI::Rect row_bounds{
            panel.x,
            tree_top + static_cast<float>(visible_row) * row_height * scale,
            panel.width,
            row_height * scale,
        };
        const bool is_selected = m_model.is_selected(item.path);
        const bool is_hovered = (m_hovered_row && *m_hovered_row == visible_row);
        const bool is_drag_source = m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row == visible_row;
        const bool is_drop_target = m_is_dragging_item && m_drag_target_row.has_value() && *m_drag_target_row == visible_row;

        const UI::Rect highlight_rect{
            panel.x + 4.0F * scale,
            row_bounds.y + 1.0F * scale,
            panel.width - 8.0F * scale,
            row_height * scale - 2.0F * scale
        };

        if (is_drop_target) {
          surface.fill_rectangle(device_context, row_bounds, UI::Theme::Color{35, 110, 190, 90});
          surface.draw_rectangle(device_context, row_bounds, surface.m_palette.accent);
        } else if (is_drag_source) {
          surface.fill_rectangle(device_context, row_bounds, UI::Theme::Color{255, 255, 255, 20});
        } else if (is_selected) {
          surface.fill_rectangle(device_context, row_bounds, UI::Theme::Color{14, 75, 130, 255});
          if (m_model.get_selected_path() && *m_model.get_selected_path() == item.path) {
            const UI::Rect left_bar{
                panel.x, row_bounds.y + 1.0F * scale,
                3.0F * scale, row_bounds.height - 2.0F * scale
            };
            surface.fill_rectangle(device_context, left_bar, surface.m_palette.accent);
          }
        } else if (is_hovered) {
          surface.fill_rectangle(device_context, row_bounds, surface.m_palette.hover_background);
        }

        const UI::Theme::Color current_row_bg = is_selected
            ? UI::Theme::Color{14, 75, 130, 255}
            : (is_hovered ? surface.m_palette.hover_background : surface.m_palette.sidebar_background);

        const float indent_x = panel.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
        const UI::Theme::Color guide_color{85, 92, 105, 190};

        for (std::size_t level = 0; level < item.depth; ++level) {
          const int guide_x = round_to_int(
              panel.x + (13.0F + static_cast<float>(level) * 16.0F) * scale);

          bool line_active = false;
          for (std::size_t next = item_index + 1; next < items.size(); ++next) {
            if (items[next].depth <= level + 1) {
              line_active = (items[next].depth == level + 1);
              break;
            }
          }

          if (level == item.depth - 1 || line_active) {
            surface.draw_line(device_context, guide_x, round_to_int(row_bounds.y),
                              guide_x, round_to_int(row_bounds.bottom()),
                              guide_color);
          }
        }

        const bool is_cut = m_model.is_cut_path(item.path);
        const UI::Theme::Color icon_color = is_cut
            ? UI::Theme::Color{140, 140, 140, 120}
            : (is_selected
                   ? (surface.m_palette.is_dark ? UI::Theme::Color{255, 255, 255, 255} : surface.m_palette.accent)
                   : surface.m_palette.text_muted);

        if (item.directory) {
          const int arrow_x = round_to_int(indent_x + 3.0F * scale);
          const int arrow_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
          if (arrow_x + 8.0F * scale < panel.right()) {
            const std::string chevron_path = item.expanded
                                                 ? "chevron-down.svg"
                                                 : "chevron-right.svg";
            surface.draw_svg_icon(
                device_context, chevron_path, arrow_x, arrow_y,
                std::max(round_to_int(8.0F * scale), 7),
                icon_color,
                current_row_bg);
          }
          const int folder_x = round_to_int(indent_x + 19.0F * scale);
          if (folder_x + 16.0F * scale < panel.right()) {
            const std::string folder_path =
                UI::Editor::folder_icon_asset_for_path(item.path, item.expanded);
            const int folder_size = std::max(round_to_int(14.0F * scale), 11);
            surface.draw_svg_icon(
                device_context, folder_path, folder_x, arrow_y,
                folder_size,
                icon_color,
                current_row_bg,
                !is_cut);
          }
        } else {
          const int icon_x = round_to_int(indent_x + 19.0F * scale);
          const int icon_y = round_to_int(row_bounds.y + row_bounds.height * 0.5F);
          if (icon_x + 14.0F * scale < panel.right()) {
            const std::string icon_asset =
                UI::Editor::file_icon_asset_for_path(item.path);
            surface.draw_svg_icon(
                device_context, icon_asset, icon_x, icon_y,
                std::max(round_to_int(14.0F * scale), 11),
                icon_color,
                current_row_bg,
                !is_cut);
          }
        }

        const float label_x = indent_x + (item.directory ? 30.0F : 36.0F) * scale;
        if (label_x < panel.right()) {
          const float available_width = panel.right() - label_x - 10.0F * scale;
          if (available_width > 0.0F) {
            const std::string label = ellipsize(
                device_context, *surface.m_small_font, item.label,
                round_to_int(available_width));
            const UI::Theme::Color text_color = is_cut
                ? UI::Theme::Color{160, 165, 175, 130}
                : (is_selected ? UI::Theme::Color{255, 255, 255, 255} : UI::Theme::Color{242, 244, 250, 255});
            surface.draw_text(device_context, *surface.m_small_font, label, label_x,
                              row_bounds.y + row_bounds.height * 0.5F,
                              text_color);
          }
        }
      }

      // Sticky Explorer Headers
      const auto sticky_indices = get_sticky_items();
      for (std::size_t i = 0; i < sticky_indices.size(); ++i) {
          const std::size_t item_index = sticky_indices[i];
          if (item_index >= items.size()) continue;

          const UI::Editor::ProjectTreeItem &item = items[item_index];
          const float sticky_y = tree_top + static_cast<float>(i) * row_height * scale;
          const UI::Rect sticky_bounds{
              panel.x,
              sticky_y,
              panel.width,
              row_height * scale,
          };

          const bool is_sticky_hovered = (m_hovered_sticky_index && *m_hovered_sticky_index == item_index);
          const UI::Theme::Color sticky_bg = is_sticky_hovered 
              ? surface.m_palette.hover_background 
              : surface.m_palette.sidebar_background;

          surface.fill_rectangle(device_context, sticky_bounds, sticky_bg);
          if (i == sticky_indices.size() - 1) {
            surface.draw_line(device_context, round_to_int(panel.x), round_to_int(sticky_bounds.bottom()),
                              round_to_int(panel.right()), round_to_int(sticky_bounds.bottom()),
                              surface.m_palette.border);
          }

          const float indent_x = panel.x + (10.0F + static_cast<float>(item.depth) * 16.0F) * scale;
          const int arrow_x = round_to_int(indent_x + 3.0F * scale);
          const int arrow_y = round_to_int(sticky_bounds.y + row_height * 0.5F * scale);

          surface.draw_svg_icon(
              device_context, "chevron-down.svg",
              arrow_x,
              arrow_y,
              std::max(round_to_int(8.0F * scale), 7),
              UI::Theme::Color{220, 225, 235, 255},
              sticky_bg);

          const int folder_x = round_to_int(indent_x + 19.0F * scale);
          const std::string folder_path =
              UI::Editor::folder_icon_asset_for_path(item.path, true);
          surface.draw_svg_icon(
              device_context, folder_path,
              folder_x,
              arrow_y,
              std::max(round_to_int(14.0F * scale), 11),
              UI::Theme::Color{235, 240, 250, 255},
              sticky_bg,
              true);

          const float label_x = indent_x + 30.0F * scale;
          const std::string label = ellipsize(
              device_context, *surface.m_small_font, item.label,
              std::max(round_to_int(panel.right() - label_x - 8.0F * scale), 1));
          surface.draw_text(device_context, *surface.m_small_font, label, label_x,
                            arrow_y,
                            UI::Theme::Color{250, 252, 255, 255});
      }

      RestoreDC(device_context, clip_saved);

      // Project Scrollbar
      m_project_scrollbar.set_track_bounds(scrollbar_bounds(layout));
      m_project_scrollbar.set_metrics(items.size(), row_count);
      m_project_scrollbar.scroll_to(first);
      if (m_project_scrollbar.is_needed()) {
        const UI::Rect thumb_bounds = m_project_scrollbar.get_thumb_bounds();
        const UI::Theme::Color thumb_color = m_project_scrollbar.is_dragging()
            ? UI::Theme::Color{255, 255, 255, 115}
            : (m_hovered_scrollbar ? UI::Theme::Color{255, 255, 255, 75} : UI::Theme::Color{255, 255, 255, 40});
        surface.fill_rectangle(device_context, thumb_bounds, thumb_color);
      }
    }

    // Ghost drag preview badge
    if (m_is_dragging_item && m_drag_source_row.has_value() && *m_drag_source_row < items.size()) {
      const auto& dragged = items[*m_drag_source_row];
      const std::string badge_label = dragged.label;
      const int text_w = surface.m_small_font ? surface.m_small_font->getTextWidth(device_context, badge_label) : 40;
      const float badge_w = static_cast<float>(text_w) + 36.0F * scale;
      const float badge_h = 24.0F * scale;
      const UI::Rect badge_rect{m_drag_current_x + 12.0F * scale, m_drag_current_y + 12.0F * scale, badge_w, badge_h};

      surface.fill_rounded_rectangle(device_context, badge_rect, UI::Theme::Color{24, 28, 38, 245}, 4.0F * scale);
      surface.draw_rectangle(device_context, badge_rect, surface.m_palette.accent);

      const int badge_icon_x = round_to_int(badge_rect.x + 12.0F * scale);
      const int badge_icon_y = round_to_int(badge_rect.y + badge_h * 0.5F);
      if (dragged.directory) {
        const std::string folder_path =
            UI::Editor::folder_icon_asset_for_path(dragged.path, false);
        surface.draw_svg_icon(device_context, folder_path, badge_icon_x, badge_icon_y,
                              std::max(round_to_int(12.0F * scale), 10),
                              UI::Theme::Color{255, 255, 255, 255}, UI::Theme::Color{24, 28, 38, 255},
                              true);
      } else {
        const std::string icon_asset = UI::Editor::file_icon_asset_for_path(dragged.path);
        surface.draw_svg_icon(device_context, icon_asset, badge_icon_x, badge_icon_y,
                              std::max(round_to_int(12.0F * scale), 10),
                              UI::Theme::Color{255, 255, 255, 255}, UI::Theme::Color{24, 28, 38, 255},
                              true);
      }

      if (surface.m_small_font) {
        surface.draw_text(device_context, *surface.m_small_font, badge_label,
                          badge_rect.x + 22.0F * scale,
                          badge_rect.y + badge_rect.height * 0.5F,
                          UI::Theme::Color{255, 255, 255, 255});
      }
    }
  }

  // Draw Right Border separating sidebar from editor with blue accent highlight when hovered or resizing
  const bool show_accent = m_resize_hovered || m_resizing;
  const UI::Theme::Color splitter_color = show_accent
      ? surface.m_palette.accent
      : surface.m_palette.border;

  const float splitter_x = panel.right() - scale;
  surface.draw_line(device_context,
                    round_to_int(splitter_x),
                    round_to_int(panel.y),
                    round_to_int(splitter_x),
                    round_to_int(panel.bottom()),
                    splitter_color);

  if (show_accent) {
    surface.fill_rectangle(
        device_context,
        UI::Rect{
            splitter_x - 1.0F * scale,
            panel.y,
            2.0F * scale,
            panel.height},
        surface.m_palette.accent);
  }
}

std::size_t ToolSidebar::viewport_row_count(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float available = layout.tool_sidebar_bounds.height - header_height * scale;
  if (available <= 0.0F) return 0;
  return static_cast<std::size_t>(available / (row_height * scale));
}

std::optional<std::size_t> ToolSidebar::row_from_point(
    const UI::Editor::StudioEditorLayoutResult &layout,
    float point_y) const noexcept {
  const float scale = layout.dpi_scale;
  const float tree_top = layout.tool_sidebar_bounds.y + header_height * scale;
  if (point_y < tree_top || point_y >= layout.tool_sidebar_bounds.bottom()) {
    return std::nullopt;
  }
  const auto idx = static_cast<std::size_t>((point_y - tree_top) / (row_height * scale));
  const auto items = m_model.get_project_items();
  const std::size_t actual_idx = m_model.get_scroll_offset() + idx;
  if (actual_idx < items.size()) {
    return idx;
  }
  return std::nullopt;
}

UI::Rect ToolSidebar::scrollbar_bounds(
    const UI::Editor::StudioEditorLayoutResult &layout) const noexcept {
  const float scale = layout.dpi_scale;
  const float top = layout.tool_sidebar_bounds.y + header_height * scale;
  const float width = 8.0F * scale;
  return UI::Rect{
      layout.tool_sidebar_bounds.right() - width,
      top,
      width,
      std::max(layout.tool_sidebar_bounds.bottom() - top, 0.0F)
  };
}

std::vector<std::size_t> ToolSidebar::get_sticky_items() const {
  std::vector<std::size_t> sticky;
  const auto items = m_model.get_project_items();
  const std::size_t scroll_offset = m_model.get_scroll_offset();
  if (scroll_offset == 0 || items.empty()) {
      return sticky;
  }

  std::size_t current_idx = std::min(scroll_offset, items.size() - 1);
  std::size_t current_depth = items[current_idx].depth;

  for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(current_idx); i >= 0; --i) {
      const auto &item = items[static_cast<std::size_t>(i)];
      if (item.directory && item.depth < current_depth) {
          sticky.push_back(static_cast<std::size_t>(i));
          current_depth = item.depth;
          if (current_depth == 0) break;
      }
  }

  std::reverse(sticky.begin(), sticky.end());
  return sticky;
}

} // namespace Zenvra::Platform::Win32::Components
